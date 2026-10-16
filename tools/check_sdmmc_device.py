#!/usr/bin/env python3
"""Live SDMMC/LCD scenario; never formats media, only cleans its unique test directory."""
import argparse,sys,json,time,uuid,urllib.request,urllib.parse,socket,struct,pathlib
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent));from os_client import Client,RpcError
urllib.request.install_opener(urllib.request.build_opener(urllib.request.ProxyHandler({})))
parser=argparse.ArgumentParser(description="Nonformatting SDMMC, LCD and sensor scenario on an explicitly identified device")
parser.add_argument('--url',required=True);parser.add_argument('--mac',required=True);parser.add_argument('--board',required=True);parser.add_argument('--output',type=pathlib.Path,required=True);args=parser.parse_args()
c=Client(url=args.url,timeout=12);checks=[];report={'checks':checks};root='/storage/user/wishos-check-'+uuid.uuid4().hex[:10];created=[];nfs=False

def call(p,m='read',a=None):return c.call(p,m,a)
def wait(fn,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  result=fn()
  if result:return result
  time.sleep(.02)
 raise TimeoutError('device condition')
def op(m):
 r=call('/sys/operations','request',{'path':'/dev/sd','method':m,'args':{}});ident=r['completion_id']
 try:
  result=wait(lambda:(s if (s:=call('/sys/operations','read',{'id':ident})).get('ready') else None))
  assert result['event']=='operation_completed',result
  return result
 finally:call('/sys/operations','release',{'id':ident})
def expect(error,fn):
 try:fn();raise AssertionError('accepted '+error)
 except RpcError as e:assert str(e)==error,str(e)
def read(file,offset=0,length=1024):return bytes.fromhex(call('/fs','read',{'file':file,'offset':str(offset),'length':length})['hex'])
def u(*xs):return struct.pack('>'+'I'*len(xs),*xs)
def opaque(b):return u(len(b))+b+b'\0'*((-len(b))%4)
try:
 report['board']=call('/sys/board');assert report['board']['mac']==args.mac.upper() and report['board']['board']==args.board
 wait(lambda:not call('/sys/apps').get('pending'));assert call('/sys/runtime')['file'].startswith('/storage/flash/factory/');checks.append('managed_filesystem_launcher_running')
 report['resources_before']=call('/sys/resources');assert report['resources_before']['heaps'][1]['capacity_bytes']>0;checks.append('external_heap_and_vm_admission')
 expect('fixed_clock_unavailable',lambda:call('/dev/sd','configure',{'clock_hz':25000000,'read_only':True}));assert call('/dev/sd')['clock_mode']=='auto';checks.append('unsupported_fixed_clock_rejects_complete_change')
 call('/dev/sd','configure',{'read_only':True});op('probe');s=call('/dev/sd');assert s['link_ready'] and not s['mounted'];report['link']=s;checks.append('content_independent_link_probe_separate_from_mount')
 op('mount');page=call('/fs','list',{'path':'/storage/user','limit':8});report['initial_root_entries']=len(page['entries']);expect('read_only',lambda:call('/fs','mkdir',{'path':root}));checks.append('read_only_mount_rejects_write')
 op('unmount');call('/dev/sd','configure',{'read_only':False});op('mount');op('space');call('/fs','mkdir',{'path':root});created.append(root)
 data=bytes((i*73+19)%256 for i in range(8192));file=root+'/中文-Ä.txt';created.append(file)
 for at in range(0,len(data),1024):assert call('/fs','write',{'file':file,'offset':str(at),'hex':data[at:at+1024].hex(),'truncate':at==0})['written']==1024
 call('/fs','flush',{'file':file});assert b''.join(read(file,at) for at in range(0,8192,1024))==data;assert read(file,3073,511)==data[3073:3584];checks.append('sd_utf8_file_chunk_write_read_and_random_seek')
 # An app stored on SD holds the provider until the application is replaced.
 code=root+'/main.lua';created.append(code);call('/fs','write',{'file':code,'text':'function on_start() end','truncate':True});call('/sys/runtime','load',{'file':code,'root':root});expect('storage_in_use',lambda:call('/dev/sd','unmount'));call('/sys/apps','home');wait(lambda:not call('/sys/apps').get('pending'));checks.append('sd_application_provider_lease_blocks_unmount_and_releases_on_home')
 # Simultaneous sensor sampling, LCD DMA and network/file access.
 call('/dev/imu','configure',{});call('/dev/touch','configure',{});view=call('/sys/ui')['view_id'];max_ms=0
 for i in range(12):
  start=time.monotonic();call('/dev/imu','measure');call('/dev/touch','measure');call('/sys/ui','update',{'view_id':view,'values':{'network':f'WishOS · 并发测试 {i}'}});assert read(file,777,256)==data[777:1033];call('/sys/stats');max_ms=max(max_ms,(time.monotonic()-start)*1000);time.sleep(.04)
 wait(lambda:not call('/dev/display')['active'] and not call('/dev/display')['pending']);imu=call('/dev/imu');touch=call('/dev/touch');assert imu['valid'] and touch['valid'];report['imu']=imu;report['touch']=touch;report['concurrent_cycle_max_ms']=round(max_ms,1);checks.append('lcd_dma_sensor_sd_http_concurrent_without_reboot')
 # The same public request/callback metadata works for LCD and EPD adapters.
 r=call('/sys/operations','request',{'path':'/sys/ui','method':'refresh','args':{'view_id':view}});ident=r['completion_id']
 try:
  result=wait(lambda:(s if (s:=call('/sys/operations','read',{'id':ident})).get('ready') else None));assert result['event']=='completed' and result['data']['completion']['kind']=='transport_complete';report['lcd_refresh_ms']=result['data']['total_ms'];checks.append('ui_request_retains_dma_completion_for_runtime_callbacks')
 finally:call('/sys/operations','release',{'id':ident})
 # Real NFSv3 TCP uses the same mounted SD provider, including Unicode paths.
 call('/services/nfs','start',{'provider':'user','export_path':'/sd','port':2049,'read_only':True,'bytes_per_second':0});nfs=True
 with socket.create_connection((urllib.parse.urlparse(args.url).hostname,2049),timeout=8) as conn:
  def exact(n):
   buf=b''
   while len(buf)<n:
    part=conn.recv(n-len(buf));assert part;buf+=part
   return buf
  def rpc(xid,prog,proc,args):
   body=u(xid,0,2,prog,3,proc,0,0,0,0)+args;conn.sendall(u(len(body)|0x80000000)+body);sz=struct.unpack('>I',exact(4))[0];r=exact(sz&0x7fffffff);assert r[:24]==u(xid,1,0,0,0,0);return r[24:]
  def handle(r):assert r[:4]==u(0),r.hex();n=struct.unpack('>I',r[4:8])[0];return r[8:8+n]
  h=handle(rpc(1,100005,1,opaque(b'/sd')));directory=handle(rpc(2,100003,3,opaque(h)+opaque(root.rsplit('/',1)[1].encode())));f=handle(rpc(3,100003,3,opaque(directory)+opaque('中文-ä.TXT'.encode())));r=rpc(4,100003,6,opaque(f)+struct.pack('>Q',3073)+u(511));assert r[:8]==u(0,1),r.hex();n=struct.unpack('>I',r[100:104])[0];assert r[104:104+n]==data[3073:3584];checks.append('real_nfs_v3_sd_unicode_case_lookup_and_random_read')
 call('/services/nfs','stop');nfs=False
 op('unmount');op('mount');assert read(file,4096)==data[4096:5120];checks.append('durable_data_survives_remount')
 # Exercise the Lua callback path, rather than only the host observation API.
 callback_marker='lcd-callback-'+uuid.uuid4().hex[:8]
 callback_source="function on_start() local r=notes.call('/sys/ui','open',{path='/storage/flash/factory/ui/home.json'}) assert(r.ok,r.error) local q=notes.request('/sys/ui','refresh',{view_id=r.result.view_id},function(e) assert(e.event=='completed' and e.data.completion.kind=='transport_complete') assert(notes.call('/sys/logs','write',{message='"+callback_marker+"'}).ok) end) assert(q.ok,q.error) end"
 call('/sys/runtime','load',{'source':callback_source,'memory_bytes':49152})
 wait(lambda:call('/sys/runtime').get('callbacks')==0);assert not call('/sys/runtime')['fault'];checks.append('lua_display_completion_callback_runs_and_releases_observation')
 call('/sys/apps','home');wait(lambda:not call('/sys/apps').get('pending'))
 # Native workers use separate VMs and scheduler-approved CPU affinity.
 worker_ids=[]
 try:
  for cpu in range(min(2,report['board']['cores'])):
   r=call('/sys/workers','submit',{'core':cpu,'source':'function compute(x) local s=0 for i=1,20000 do s=s+i end return s end','input':{},'memory_bytes':16384,'instruction_budget':1000000});assert r['core']==cpu;worker_ids.append(r['id'])
  for ident in worker_ids:
   result=wait(lambda:(s if (s:=call('/sys/workers','read',{'id':ident})).get('state') in ('completed','failed') else None));assert result['ok'] and result['result']==200010000
  checks.append('parallel_cpu_affinity_workers_complete_while_network_responds')
 finally:
  for ident in worker_ids:call('/sys/workers','release',{'id':ident})
 for source in ['function on_start() while true do end end', "function on_start() local x=string.rep('x',1000000) end"]:
  try:call('/sys/runtime','load',{'source':source,'memory_bytes':24576,'instruction_budget':3000});raise AssertionError('unbounded app accepted')
  except RpcError:assert call('/sys/runtime')['fault']
  call('/sys/runtime','stop');call('/sys/stats')
 call('/sys/apps','home');wait(lambda:not call('/sys/apps').get('pending'));checks.append('instruction_and_memory_faults_preserve_os_and_recover_launcher')
 report['resources_after']=call('/sys/resources');report['stats']=call('/sys/stats');assert call('/sys/runtime')['fault']==False;report['ok']=True
finally:
 try:
  if nfs:call('/services/nfs','stop')
  for file in reversed(created):call('/fs','remove',{'file':file})
  call('/sys/apps','home');wait(lambda:not call('/sys/apps').get('pending'));report['cleanup_ok']=True
 except Exception as e:report['cleanup_error']=str(e)
 args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n')
print(json.dumps(report,ensure_ascii=False,indent=2))
