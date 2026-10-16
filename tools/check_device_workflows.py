#!/usr/bin/env python3
"""Explicit disposable-device fault tests. Requires pyserial and websockets.
The fixture emulates upstream RPC/result delivery, not a vendor model account.
"""
import argparse, asyncio, hashlib, json, logging, socket, struct, time, uuid, subprocess
from pathlib import Path
from serial.tools.list_ports import comports
from os_client import Client, RpcError
from websockets.asyncio.server import serve
from websockets.frames import OP_PONG

parser=argparse.ArgumentParser()
parser.add_argument('--serial-id',required=True)
parser.add_argument('--host',required=True,help='Reachable hostname of this computer; use its .local name to verify mDNS')
parser.add_argument('--output',required=True,type=Path)
parser.add_argument('--esptool',type=Path,help='Optional trusted esptool executable for a real reset during partial transfer; never writes Flash')
args=parser.parse_args()
logging.getLogger('websockets.server').setLevel(logging.CRITICAL)

class Peer:
    def __init__(self,ws):
        self.ws=ws;self.boot=None;self.waiters={};self.sequence=0;self.lock=asyncio.Lock();self.ready=asyncio.Event();self.silent=False
        send_frame=ws.protocol.send_frame
        ws.protocol.send_frame=lambda frame: None if self.silent and frame.opcode==OP_PONG else send_frame(frame)
    async def run(self):
        data=bytearray();stream=None
        try:
            async for frame in self.ws:
                magic,ident,offset,total=struct.unpack('<4sIHH',frame[:12]);assert magic==b'WOS1'
                if offset==0:data=bytearray();stream=ident
                assert stream==ident and offset==len(data)
                data.extend(frame[12:])
                if len(data)==total:
                    result=json.loads(data)
                    if result.get('type')=='hello':self.boot=result['boot_id'];self.ready.set()
                    elif result.get('id') in self.waiters:
                        future=self.waiters[result['id']]
                        if not future.done():future.set_result(result)
        finally:
            for future in self.waiters.values():
                if not future.done():future.set_exception(ConnectionError('fixture route closed; no automatic mutation replay'))
    async def begin(self,path,method='read',params=None,long_id=False):
        await self.ready.wait();await self.lock.acquire();self.sequence+=1
        ident=uuid.uuid4().hex+('x'*1800 if long_id else '')
        request={'id':ident,'path':path,'method':method,'args':params or {},'expected_boot_id':self.boot}
        data=json.dumps(request,separators=(',',':')).encode();assert len(data)<=4096
        frames=[struct.pack('<4sIHH',b'WOS1',self.sequence,i,len(data))+data[i:i+1012] for i in range(0,len(data),1012)]
        future=asyncio.get_running_loop().create_future();self.waiters[ident]=future
        return ident,frames,future
    async def finish(self,ident,frames,future):
        try:
            for frame in frames:await self.ws.send(frame)
            return await asyncio.wait_for(future,15)
        finally:self.waiters.pop(ident,None);self.lock.release()
    async def call(self,path,method='read',params=None):
        return await self.finish(*(await self.begin(path,method,params)))

class Endpoint:
    def __init__(self):self.peer=None;self.connections=0;self.server=None
    async def handle(self,ws):
        assert ws.request.headers.get('Authorization')=='Bearer fixture-token'
        assert ws.request.headers.get('X-Wishos-Fixture')=='control'
        peer=Peer(ws);self.peer=peer;self.connections+=1;await peer.run()
    async def start(self,port=0):
        self.server=await serve(self.handle,'0.0.0.0',port,ping_interval=None);return self.server.sockets[0].getsockname()[1]
    async def stop(self):
        if self.server:self.server.close();await self.server.wait_closed();self.server=None

async def main():
    ports=[p.device for p in comports() if (p.serial_number or '').upper()==args.serial_id.upper()]
    assert len(ports)==1,'Expected one explicitly identified disposable device'
    c=Client(port=ports[0],timeout=15);report={'checks':[]};relay=Endpoint();lan=Endpoint();reserved=socket.socket();reserved.bind(('',0));lan_port=reserved.getsockname()[1]
    async def rpc(path,method='read',params=None):return await asyncio.to_thread(c.call,path,method,params)
    async def wait(fn,seconds=60):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=await fn()
            if result:return result
            await asyncio.sleep(.05)
        raise TimeoutError('condition deadline')
    async def selected(name):
        r=await rpc('/services/link')
        return r if r['connected'] and not r['activation_pending'] and r['endpoint_id']==name else None
    async def operation(path,method):
        async def prepared():
            state=await rpc(path)
            return state if state.get('operation',{}).get('state')!='running' else None
        if method=='mount':
            async def settled():return not (await rpc('/sys/apps')).get('pending')
            await wait(settled)
        state=await wait(prepared)
        if method=='mount' and state.get('mounted'):return {'ok':True}
        ident=(await rpc('/sys/operations','request',{'path':path,'method':method,'args':{}}))['completion_id']
        async def done():
            r=await rpc('/sys/operations','read',{'id':ident});return r if r['ready'] else None
        r=await wait(done);await rpc('/sys/operations','release',{'id':ident});assert r['data'].get('ok',True),r
    async def transfer_call(method,params):
        end=time.monotonic()+20
        while True:
            try:return await rpc('/sys/transfers',method,params)
            except RpcError as e:
                if str(e) not in ('resource_busy','spi_busy','operation_active') or time.monotonic()>end:raise
                await asyncio.sleep(.05)
    original={'enabled':False}
    try:
        report['board']=await rpc('/sys/board');assert report['board']['mcu']=='ESP32-C3'
        original=(await rpc('/services/link'))['desired']
        async def application_idle():return not (await rpc('/sys/apps')).get('pending')
        await wait(application_idle)
        await rpc('/sys/runtime','stop');await rpc('/dev/wifi','resume_auto')
        async def online():
            r=await rpc('/dev/wifi');return r if r['connected'] else None
        await wait(online);relay_port=await relay.start()
        settings={'enabled':True,'endpoints':[{'id':'lan','host':args.host,'port':lan_port,'path':'/os/device?id=fixture','tls':False,'headers':{'Authorization':'Bearer fixture-token','X-Wishos-Fixture':'control'}},{'id':'relay','host':args.host,'port':relay_port,'path':'/os/device?id=fixture','tls':False,'headers':{'Authorization':'Bearer fixture-token','X-Wishos-Fixture':'control'}}],'attempt_timeout_ms':20000,'retry_ms':300,'probe_interval_ms':1000,'idle_ms':250}
        await rpc('/services/link','configure',{'settings':settings});await wait(lambda:selected('relay'));await relay.peer.ready.wait();report['checks'].append('ordered_failover_to_second_endpoint')
        assert (await relay.peer.call('/sys/board'))['result']['mac']==report['board']['mac'];report['checks'].append('rpc_over_mdns_resolved_hostname' if args.host.endswith('.local') else 'rpc_over_explicit_endpoint')
        # Hold a fragmented request while the preferred endpoint becomes reachable.
        peer=relay.peer;ident,frames,future=await peer.begin('/sys/stats',long_id=True);await peer.ws.send(frames[0]);reserved.close();await lan.start(lan_port)
        await asyncio.sleep(2);assert (await rpc('/services/link'))['endpoint_id']=='relay'
        reply=await peer.finish(ident,frames[1:],future);assert reply['ok'];await wait(lambda:selected('lan'));await lan.peer.ready.wait();report['checks'].append('promotion_waits_for_complete_rpc_and_response')
        # Reconfigure through the channel being reconfigured, and observe its reply.
        settings['probe_interval_ms']=0
        for endpoint in settings['endpoints']:endpoint.update(ping_interval_ms=500,pong_timeout_ms=300,pong_misses=1)
        prior=(await rpc('/services/link'))['revision'];peer=lan.peer
        reply=await peer.call('/services/link','configure',{'settings':settings});assert reply['ok'],reply
        async def replaced():
            r=await selected('lan');return r if r and lan.peer is not peer else None
        await wait(replaced);await lan.peer.ready.wait();assert (await rpc('/services/link'))['revision']==prior+1;report['checks'].append('in_channel_configuration_reply_before_retirement')
        # The socket remains open but no longer answers ping: it must not stay selected forever.
        lan.peer.silent=True;await wait(lambda:selected('relay'),20);report['checks'].append('silent_peer_heartbeat_failover')
        bad=json.loads(json.dumps(settings));bad['endpoints'][1]['id']='lan';prior=(await rpc('/services/link'))['revision']
        try:await rpc('/services/link','configure',{'settings':bad});raise AssertionError('duplicate route accepted')
        except RpcError:pass
        assert (await rpc('/services/link'))['revision']==prior;report['checks'].append('invalid_configuration_preserves_applied_route')
        # Real Lua instances process application steps around an explicit stop/load handover.
        # This plan releases the old VM before admitting the next, for devices that cannot
        # host both at once. Atomic load remains available and preserves the old VM on rejection.
        receiver="local ai=notes.require('/system/lib/ai.lua') function on_start() ai.listen(function(a) assert(notes.call('/sys/logs','write',{message='%s:'..(a.data.text or '')}).ok) end) %s end"
        old=receiver%('old',"assert(notes.call('/services/ai','submit',{input={text='fixture'}}).ok)")
        new=receiver%('new','')
        await rpc('/sys/runtime','load',{'source':old,'memory_bytes':49152})
        request=await rpc('/services/remote','peek');assert request['payload']['format']=='wishos-ai/2'
        result={'format':'wishos-result/2','operation_id':'replace-fixture','actions':[{'type':'application','data':{'text':'before'}},{'type':'system','path':'/sys/runtime','method':'stop','args':{}},{'type':'system','path':'/sys/runtime','method':'load','args':{'source':new,'memory_bytes':49152}},{'type':'application','recipient':'foreground','data':{'text':'after'}}]}
        await rpc('/services/remote','complete',{'id':request['id'],'ok':True,'result':result})
        async def finished():
            tasks=(await rpc('/services/ai'))['tasks']
            for task in tasks:
                if task['state']=='failed':raise AssertionError(await rpc('/services/ai','read',{'id':task['id']}))
            return tasks if tasks and all(t['state']=='completed' for t in tasks) else None
        await wait(finished);messages=[];after=0
        while True:
            logs=await rpc('/sys/logs','read',{'after':after,'limit':8});messages.extend(x['message'] for x in logs['records']);after=logs['cursor']
            if not logs['more']:break
        assert 'old:before' in messages and 'new:after' in messages and messages.index('old:before')<messages.index('new:after'),messages
        report['checks'].append('ordered_actions_acknowledge_origin_then_stop_load_and_foreground')
        for done in (await rpc('/services/ai'))['tasks']:await rpc('/services/ai','release',{'id':done['id']})
        # Stage a long sequence without embedding it in the small completion reply.
        await operation('/dev/flash','mount');await operation('/dev/flash','space')
        task=(await rpc('/services/ai','submit',{'input':{'text':'long plan'}}))['id'];remote=(await rpc('/services/remote','peek'))['id']
        steps=[{'type':'system','path':'/sys/clock','method':'read','args':{}} for _ in range(40)]+[{'type':'application','recipient':'foreground','data':{'text':'long-plan'}}]
        body=('\n'.join(json.dumps(x,separators=(',',':')) for x in steps)+'\n').encode()
        started=await transfer_call('begin',{'bytes':str(len(body)),'files':1,'remote_request_id':remote});tid=started['id']
        await transfer_call('file',{'id':tid,'path':'actions.jsonl','bytes':str(len(body)),'sha256':hashlib.sha256(body).hexdigest()})
        for offset in range(0,len(body),1024):await transfer_call('chunk',{'id':tid,'offset':str(offset),'hex':body[offset:offset+1024].hex()})
        await transfer_call('close_file',{'id':tid});await transfer_call('commit',{'id':tid})
        await rpc('/services/remote','complete',{'id':remote,'ok':True,'result':{'format':'wishos-result/2','operation_id':'long-fixture','transfer_id':tid,'sequence':{'path':'actions.jsonl','format':'jsonl'}}})
        await wait(finished);assert (await rpc('/services/ai','read',{'id':task}))['step']==41;await rpc('/services/ai','release',{'id':task});await rpc('/fs','remove',{'file':started['root']+'/actions.jsonl'});await rpc('/fs','remove',{'file':started['root']});report['long_plan_cleanup']=True;report['checks'].append('verified_jsonl_plan_runs_with_bounded_memory')
        # A server that never returns a result cannot pin the request/download forever.
        task=(await rpc('/services/ai','submit',{'input':{'text':'no response'},'timeout_ms':500}))['id']
        async def expired():
            r=await rpc('/services/ai','read',{'id':task});return r if r['state']=='failed' else None
        assert (await wait(expired,5))['error']=='deadline_exceeded';await rpc('/services/ai','release',{'id':task});report['checks'].append('unresponsive_upstream_deadline_and_release')
        if args.esptool:
            for done in (await rpc('/services/ai'))['tasks']:
                if done['state'] in ('completed','failed'):await rpc('/services/ai','release',{'id':done['id']})
            await rpc('/sys/runtime','stop')
            await rpc('/services/link','configure',{'settings':settings,'persist':True})
            await wait(lambda:selected('lan'))
            await operation('/dev/flash','space')
            await rpc('/services/ai','submit',{'input':{'text':'interrupted transfer'}})
            old_remote=(await rpc('/services/remote','peek'))['id']
            partial=b'x'*4096
            staged=await transfer_call('begin',{'bytes':'4096','files':1,'remote_request_id':old_remote})
            await transfer_call('file',{'id':staged['id'],'path':'partial.bin','bytes':'4096','sha256':hashlib.sha256(partial).hexdigest()})
            await transfer_call('chunk',{'id':staged['id'],'offset':'0','hex':partial[:128].hex()})
            old_boot=c.boot_id;c.serial.close()
            await asyncio.to_thread(subprocess.run,[str(args.esptool),'--chip','esp32c3','--port',ports[0],'run'],check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
            async def port_ready():
                found=[p.device for p in comports() if (p.serial_number or '').upper()==args.serial_id.upper()]
                return found[0] if len(found)==1 else None
            port=await wait(port_ready,20);c=Client(port=port,timeout=15);await rpc('/sys/board');assert c.boot_id!=old_boot
            try:
                await rpc('/services/remote','complete',{'id':old_remote,'ok':True,'result':{},'expected_boot_id':old_boot})
                raise AssertionError('old-boot completion accepted')
            except RpcError as error:assert str(error)=='boot_changed',error
            assert (await rpc('/services/ai'))['tasks']==[]
            assert (await rpc('/sys/transfers'))['state']=='idle'
            await operation('/dev/flash','mount')
            partial_path=staged['root']+'/partial.bin.part'
            assert int((await rpc('/fs','stat',{'path':partial_path}))['size'])==128
            await rpc('/fs','remove',{'path':partial_path});await rpc('/fs','remove',{'path':staged['root']})
            async def restored():
                r=await rpc('/services/link');return r if r['connected'] and r['restore_valid'] and not r['activation_pending'] else None
            await wait(restored)
            report['checks'].append('restart_mid_transfer_preserves_partial_data_and_rejects_old_boot_work')
            report['checks'].append('saved_endpoint_policy_restores_without_reflashing')
        report['resources']=await rpc('/sys/resources');report['link']=await rpc('/services/link');report['ok']=True
    except Exception:
        try:
            report['failure_link']=await rpc('/services/link')
            report['failure_resources']=await rpc('/sys/resources')
            report['failure_runtime']=await rpc('/sys/runtime')
            report['failure_logs']=await rpc('/sys/logs','read',{'limit':8})
            report['endpoint_connections']={'lan':lan.connections,'relay':relay.connections}
        except Exception as error:report['diagnostic_error']=str(error)
        raise
    finally:
        try:await rpc('/sys/runtime','stop');await rpc('/services/link','configure',{'settings':original,'persist':True})
        except Exception as error:report['cleanup_error']=str(error)
        c.serial.close();reserved.close();await lan.stop();await relay.stop();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2))
    print(json.dumps({'ok':report.get('ok',False),'checks':report['checks']},indent=2))

if __name__=='__main__':asyncio.run(main())
