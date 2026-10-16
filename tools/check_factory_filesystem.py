#!/usr/bin/env python3
"""Verify a disposable device's factory app and web assets are managed files."""
import argparse,gzip,json,time,urllib.request,uuid
from pathlib import Path
from serial.tools.list_ports import comports
from os_client import Client,RpcError

p=argparse.ArgumentParser();p.add_argument('--serial-id',required=True);p.add_argument('--url',required=True);p.add_argument('--output',required=True,type=Path);args=p.parse_args()
ports=[p.device for p in comports()if (p.serial_number or '').upper()==args.serial_id.upper()];assert len(ports)==1
client=Client(port=ports[0],timeout=15);report={'checks':[]};originals={};timer=0x60000000|int(uuid.uuid4().hex[:7],16)
def call(path,method='read',params=None):return client.call(path,method,params)
def wait(fn):
 until=time.monotonic()+30
 while time.monotonic()<until:
  value=fn()
  if value:return value
  time.sleep(.05)
 raise TimeoutError('Factory filesystem check did not settle')
def read(file):
 data=bytearray()
 while True:
  page=call('/fs','read',{'file':file,'offset':str(len(data)),'length':1024});block=bytes.fromhex(page['hex']);data.extend(block)
  if len(block)<1024:return bytes(data)
def write(file,data):
 for at in range(0,max(1,len(data)),1024):
  part=data[at:at+1024];r=call('/fs','write',{'file':file,'offset':str(at),'hex':part.hex(),'truncate':at==0});assert r['written']==len(part)
 call('/fs','flush',{'file':file})
def home():
 wait(lambda:not call('/sys/apps').get('pending'))
 call('/sys/apps','home');return wait(lambda:(r if (r:=call('/sys/apps')).get('runtime') and not r.get('pending') else None))
def timers():return call('/sys/timers').get('timers',[])
try:
 board=call('/sys/board');assert board['mac']==args.serial_id.upper();call('/dev/wifi','resume_auto');wait(lambda:call('/dev/wifi').get('connected'));home();runtime=call('/sys/runtime');entry=runtime['file'];assert entry.startswith('/storage/flash/factory/'),runtime
 report['entry']=entry;originals[entry]=read(entry);assert originals[entry];report['checks'].append('launcher_executes_from_writable_littlefs_directory')
 try:write(entry,originals[entry]);raise AssertionError('Active app file was writable')
 except RpcError as e:assert str(e)=='file_in_use',e
 assert read(entry)==originals[entry];report['checks'].append('active_application_lease_preserves_file')
 call('/sys/runtime','stop');assert all(t['id']!=timer for t in timers())
 patched=originals[entry]+f'\nlocal previous=on_start\nfunction on_start() previous();assert(notes.call("/sys/timers","write",{{id={timer},delay_ms=60000,period_ms=60000}}).ok) end\n'.encode()
 write(entry,patched);home();assert any(t['id']==timer for t in timers());report['checks'].append('modified_factory_code_is_executed_without_reflashing')
 call('/sys/runtime','stop');assert all(t['id']!=timer for t in timers());report['checks'].append('replacement_releases_application_resources')
 web='/storage/flash/factory/control/control.js.gz';originals[web]=read(web);marker=('factory-file-'+uuid.uuid4().hex).encode();write(web,gzip.compress(gzip.decompress(originals[web])+b'\n// '+marker+b'\n',mtime=0))
 with urllib.request.build_opener(urllib.request.ProxyHandler({})).open(args.url.rstrip('/')+'/control/control.js',timeout=10)as response:
  data=response.read();assert response.headers.get('Content-Encoding')=='gzip';assert marker in gzip.decompress(data)
 report['checks'].append('http_serves_updated_filesystem_asset')
 for file,data in originals.items():write(file,data)
 home();assert all(t['id']!=timer for t in timers());report['checks'].append('original_factory_files_restored_and_launcher_reactivated');report['ok']=True
finally:
 if not report.get('ok'):
  try:
   call('/sys/runtime','stop')
   for file,data in originals.items():write(file,data)
   home();report['restored']=True
  except Exception as e:report['restore_error']=str(e)
 client.serial.close();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
