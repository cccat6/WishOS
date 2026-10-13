#!/usr/bin/env python3
"""Explicit, identity-checked runtime validation. Never chooses an arbitrary serial port."""
import argparse,json,time,uuid,urllib.request
from pathlib import Path
import serial
from serial.tools.list_ports import comports
p=argparse.ArgumentParser();p.add_argument('--serial-mac',required=True);p.add_argument('--check-time',action='store_true');p.add_argument('--wifi-config',type=Path);p.add_argument('--format-storage',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
ports=[p for p in comports() if (p.serial_number or '').upper()==a.serial_mac.upper()]
assert len(ports)==1,'Expected exactly one matching USB serial identity'
s=serial.Serial();s.port=ports[0].device;s.baudrate=115200;s.timeout=.2;s.dtr=False;s.rts=False;s.open();time.sleep(1)
seq=0
report={'serial':a.serial_mac,'port':s.port,'checks':[]}
def rpc(path,method='read',args=None,timeout=12,ok=True):
 global seq
 seq+=1;key=f'test-{seq}';s.write((json.dumps({'id':key,'path':path,'method':method,'args':args or {}},ensure_ascii=False)+'\n').encode());end=time.monotonic()+timeout
 while time.monotonic()<end:
  line=s.readline()
  try:r=json.loads(line)
  except (ValueError,TypeError):continue
  if r.get('id')==key:
   if ok:assert r.get('ok'),(path,method,r.get('error'),r.get('result'))
   return r
 raise TimeoutError(path+' '+method)
try:
 board=rpc('/sys/board')['result'];assert board['mcu']=='ESP32-C3';report['board']=board
 report['initial_stats']=rpc('/sys/stats')['result'];report['checks'].append('identity_and_runtime_boot')
 rpc('/sys/resources','configure',{'heap_reserve_bytes':12000})
 if a.wifi_config:
  rpc("/dev/wifi","configure",{"settings":{"ap_enabled":False,"tx_power_dbm":8.5,"power_save":"off","connect_timeout_ms":30000}})
  config=json.loads(a.wifi_config.read_text());rpc('/dev/wifi','connect',{'ssid':config['ssid'],'password':config['password'],'persist':False,'hold':'until_resume'})
  for _ in range(160):
   state=rpc('/dev/wifi')['result']
   if state.get('connected'):break
   time.sleep(.25)
  report['wifi_state']={k:state.get(k) for k in ['connected','reason','enabled','radio_mode','ssid','ip','connection_pending','configuration_pending','selection','sdk_status','disconnect_reason']}
  assert state.get('connected');report['ip']=state['ip'];report['checks'].append('wifi_connected')
 if a.check_time:
  prior=rpc('/services/time')['result'];old=prior['last_sync_ms']
  if not prior['active']:rpc('/services/time','sync')
  for _ in range(80):
   ntp=rpc('/services/time')['result']
   if ntp['last_sync_ms']>old and not ntp['active']:break
   time.sleep(.25)
  assert ntp['last_sync_ms']>old and not ntp['active'],ntp
  clock=rpc('/sys/clock')['result'];assert clock['wall_clock_valid'] and abs(clock['unix_s']-time.time())<5
  report['time_sync']=ntp;report['clock']=clock;report['checks'].append('real_ntp_synchronization')
 mount=rpc('/sys/storage','mount',ok=False)
 if not mount['ok']:
  assert a.format_storage,mount;rpc('/sys/storage','format')
 base='/storage/flash/test-'+uuid.uuid4().hex[:12];rpc('/fs','mkdir',{'file':base})
 try:
  filename=base+'/中文测试.txt';payload='AgenticOS 中文 UTF-8';rpc('/fs','write',{'file':filename,'text':payload,'truncate':True});read=rpc('/fs','read',{'file':filename})['result'];assert bytes.fromhex(read['hex']).decode()==payload
  report['checks'].append('littlefs_utf8_write_read')
  source="""local timer
function on_start()
 notes.subscribe('/sys/timers');notes.subscribe('/net/http-server');notes.subscribe('/sys/workers')
 local r=notes.call('/sys/timers','write',{delay_ms=50,period_ms=100});assert(r.ok);timer=r.result.id
 assert(notes.call('/net/http-server','route',{path='/app/runtime-test',body='runtime alive'}).ok)
 local task=notes.call('/sys/workers','submit',{source='function compute(x) local s=0 for i=1,20000 do s=s+i end return s end',input={},memory_bytes=20480,instruction_budget=1000000});assert(task.ok,task.error)
end
function on_event(e)
 if e.source=='/sys/workers' then local r=notes.call('/sys/workers','read',{id=e.data.id});assert(r.ok and r.result.ok and r.result.result==200010000);notes.call('/sys/workers','release',{id=e.data.id});print('worker_verified')
 elseif e.source=='/sys/timers' then assert(notes.call('/sys/stats','read').ok) end
end"""
  rpc('/sys/runtime','load',{'source':source,'memory_bytes':28672});time.sleep(1)
  state=rpc('/sys/runtime')['result'];assert not state['fault'],state
  events=rpc('/sys/events','read',{'after':0,'limit':8})['result'];after=events['cursor'];logs=events['events']
  for _ in range(6):
   e=rpc('/sys/events','read',{'after':after,'limit':8})['result'];after=e['cursor'];logs+=e['events']
   if any(x.get('event')=='log' and x.get('data',{}).get('message')=='worker_verified' for x in logs):break
  assert any(x.get('event')=='log' and x.get('data',{}).get('message')=='worker_verified' for x in logs),logs
  if report.get('ip'):
   assert urllib.request.urlopen('http://'+report['ip']+'/app/runtime-test',timeout=5).read()==b'runtime alive'
  report['checks'].append('lua_timer_worker_http_concurrency')
  bad=rpc('/sys/runtime','load',{'source':'invalid Lua !'},ok=False);assert not bad['ok'];assert not rpc('/sys/runtime')['result']['fault']
  rpc('/sys/runtime','stop');assert rpc('/sys/timers')['result']['timers']==[];assert not rpc('/net/http-server')['result']['active']
  for code,expected in [("function on_start() while true do end end",'instruction'),("function on_start() local x=string.rep('x',1000000) end",'memory')]:
   fail=rpc('/sys/runtime','load',{'source':code,'memory_bytes':24576,'instruction_budget':3000},ok=False);assert not fail['ok'];assert rpc('/sys/runtime')['result']['fault'];rpc('/sys/runtime','stop');rpc('/sys/stats');report['checks'].append(expected+'_fault_and_recovery')
  rpc('/services/nfs','start',{'provider':'flash','export_path':'/flash'});assert rpc('/services/nfs')['result']['running'];rpc('/services/nfs','stop');assert rpc('/services/nfs')['result']['owned_heap_bytes']==0;report['checks'].append('nfs_start_stop_release')
 finally:
  rpc('/sys/runtime','stop',ok=False);rpc('/fs','remove',{'file':base+'/中文测试.txt'},ok=False);rpc('/fs','remove',{'file':base},ok=False)
 report['final_stats']=rpc('/sys/stats')['result'];report['resources']=rpc('/sys/resources')['result'];assert report['resources']['reserved_bytes']==0
 report['ok']=True
finally:
 s.close();a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2))
