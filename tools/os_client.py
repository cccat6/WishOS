#!/usr/bin/env python3
"""Bounded Notes hardware ABI client over HTTP or USB CDC; no Flash cache."""
from __future__ import annotations
import argparse, hashlib, json, time, urllib.request, urllib.error, uuid
from pathlib import Path

class RpcError(RuntimeError):
    """The device explicitly rejected a request; distinct from a lost reply."""

class Client:
    def __init__(self, url=None, port=None, timeout=10):
        self.url=url.rstrip('/') if url else None
        self.timeout=timeout
        self.serial=None
        self.boot_id=None
        self.serial_buffer=bytearray()
        if port:
            import serial
            self.serial=serial.Serial();self.serial.port=port;self.serial.baudrate=115200;self.serial.timeout=.1;self.serial.dtr=False;self.serial.rts=False;self.serial.open()
    def call(self,path,method='read',args=None,request_id=None):
        request={'id':request_id or uuid.uuid4().hex,'path':path,'method':method,'args':args or {}}
        if self.boot_id:request['expected_boot_id']=self.boot_id
        data=json.dumps(request,separators=(',',':'),ensure_ascii=False).encode()
        if len(data)>4096: raise ValueError('Request exceeds 4096 byte limit')
        if self.serial:
            self.serial.write(data+b'\n'); deadline=time.monotonic()+self.timeout
            while time.monotonic()<deadline:
                self.serial_buffer.extend(self.serial.readline())
                if len(self.serial_buffer)>32768: raise ValueError('CDC response exceeds limit')
                if not self.serial_buffer.endswith(b'\n'): continue
                line=bytes(self.serial_buffer);self.serial_buffer.clear()
                try: reply=json.loads(line)
                except (ValueError,UnicodeDecodeError): continue
                if reply.get('id')==request['id']: break
            else: raise TimeoutError('CDC response missing; inspect device before retrying mutation')
        else:
            r=urllib.request.Request(self.url+'/os/invoke',data=data,headers={'Content-Type':'application/json'})
            try:
                with urllib.request.urlopen(r,timeout=self.timeout) as res: reply=json.load(res)
            except urllib.error.HTTPError as e: reply=json.load(e)
        if self.boot_id and reply.get('boot_id')!=self.boot_id:raise RuntimeError('Device rebooted; create a new client before reusing handles')
        if reply.get('boot_id'):self.boot_id=reply['boot_id']
        if not reply.get('ok'): raise RpcError(reply.get('error',reply))
        return reply['result']
    def upload(self, source, destination):
        """Write with bounded chunks. Use a fresh version path to preserve displayed assets."""
        with Path(source).open('rb') as stream:
            # The publisher uses a fresh version path; callers can inspect interrupted uploads.
            offset=0
            while True:
                chunk=stream.read(1024)
                if not chunk and offset:break
                result=self.call('/fs','write',{'file':destination,'offset':str(offset),'hex':chunk.hex(),'truncate':offset==0})
                if int(result.get('written',-1))!=len(chunk):raise IOError('Incomplete file write')
                offset+=len(chunk)
                if not chunk:break
            return {'file':destination,'bytes':offset}
    def publish_ui(self, directory, mount='/storage/user', entry='ui.json', driver=None):
        """Upload a new immutable resource directory, then enqueue its entry; never flash UI."""
        directory=Path(directory).resolve()
        relative=Path(entry)
        if relative.is_absolute() or '..' in relative.parts:raise ValueError('Entry must be within package')
        if not (directory/relative).is_file():raise FileNotFoundError(entry)
        root=mount.rstrip('/')+'/app-'+uuid.uuid4().hex[:12]
        self.call('/fs','mkdir',{'path':root})
        for local in sorted(directory.rglob('*')):
            if local.is_symlink():raise ValueError('Symlinks are not package assets')
            dest=root+'/'+local.relative_to(directory).as_posix()
            if local.is_dir():self.call('/fs','mkdir',{'path':dest})
            elif local.is_file():self.upload(local,dest)
        args={'path':root+'/'+relative.as_posix(),'history':'replace'}
        if driver:args['driver']=driver
        result=self.call('/sys/ui','open',args)
        return {'root':root,'presentation':result}
    def publish_app(self, directory, mount='/storage/user', entry='main.lua', *, persist=False, memory_bytes=None):
        """Publish a version, verify every stored byte, then activate its runtime reference.

        Interrupted transfers never change startup. A missing activation reply is
        resolved by inspecting runtime state; on_start is never blindly replayed.
        """
        directory=Path(directory).resolve()
        relative=Path(entry)
        if relative.is_absolute() or '..' in relative.parts or not (directory/relative).is_file():
            raise ValueError('Entry must be a file within the package')
        assets=[]
        for path in sorted(directory.rglob('*')):
            if path.is_symlink():raise ValueError('Symlinks are not package assets')
            name=path.relative_to(directory).as_posix()
            if any(c in name for c in ('\\','?','#')):raise ValueError('Invalid resource path')
            if path.is_file():
                if name=='package.json':raise ValueError('package.json is generated by the publisher')
                with path.open('rb') as source:digest=hashlib.file_digest(source,'sha256').hexdigest()
                assets.append((path,name,path.stat().st_size,digest))
        prior=self.call('/sys/runtime','read')
        root=mount.rstrip('/')+'/app-'+uuid.uuid4().hex[:12]
        if any(len(root+'/'+name)>=256 for _,name,_,_ in assets):raise ValueError('Package path exceeds device path limit')
        provider=None
        if persist:
            providers=self.call('/fs','mounts')['providers']
            provider=next((p for p in providers if p['mount']==mount.rstrip('/')),None)
            if not provider or not provider['persistent']:raise ValueError('Persistent startup requires persistent storage')
        self.call('/fs','mkdir',{'path':root})
        for path in sorted(directory.rglob('*')):
            if path.is_dir():self.call('/fs','mkdir',{'path':root+'/'+path.relative_to(directory).as_posix()})
        for local,name,size,digest in assets:
            dest=root+'/'+name;self.upload(local,dest)
            info=self.call('/fs','stat',{'file':dest})
            if int(info['size'])!=size:raise IOError('Package size verification failed: '+name)
            observed=hashlib.sha256();offset=0
            while offset<size:
                r=self.call('/fs','read',{'file':dest,'offset':str(offset),'length':min(1024,size-offset)})
                data=bytes.fromhex(r['hex'])
                if not data or len(data)>min(1024,size-offset):raise IOError('Package verification read failed: '+name)
                observed.update(data);offset+=len(data)
            if observed.hexdigest()!=digest:raise IOError('Package hash verification failed: '+name)
        manifest={'format':'notes-package/1','entry':relative.as_posix(),'files':[{'path':n,'bytes':s,'sha256':h} for _,n,s,h in assets]}
        payload=json.dumps(manifest,ensure_ascii=False,separators=(',',':')).encode()
        for offset in range(0,len(payload),1024):
            piece=payload[offset:offset+1024]
            result=self.call('/fs','write',{'file':root+'/package.json','offset':str(offset),'hex':piece.hex(),'truncate':offset==0})
            if result.get('written')!=len(piece):raise IOError('Incomplete package manifest')
        target=root+'/'+relative.as_posix();args={'file':target}
        if memory_bytes is not None:args['memory_bytes']=memory_bytes
        try:active=self.call('/sys/runtime','load',args)
        except RpcError:raise
        except (OSError,TimeoutError):
            active=self.call('/sys/runtime','read')
            if not active.get('loaded') or active.get('file')!=target or active.get('fault'):raise RuntimeError('Application activation outcome unknown; inspect runtime before retrying')
        if persist:
            settings={'startup':{'auto_start':True,'file':target,'memory_bytes':memory_bytes,'prepare':[provider['prepare']] if provider.get('prepare') else []},'persist':True,'expected_revision':prior['configuration_revision']}
            if memory_bytes is not None:settings['startup']['memory_bytes']=memory_bytes
            try:self.call('/sys/runtime','configure',settings)
            except RpcError:raise
            except (OSError,TimeoutError):
                state=self.call('/sys/runtime','read')
                if not state.get('configuration_persisted') or state.get('configured_startup',{}).get('file')!=target:raise RuntimeError('Startup persistence outcome unknown')
        return {'root':root,'entry':target,'verified_files':len(assets),'persisted':persist,'previous_startup':prior.get('configured_startup'),'runtime':active}

    def present_native(self,frame:bytes,driver,force=False,view_id=None):
        if not driver:raise ValueError('Choose an advertised display profile for raw frame bytes')
        profiles=self.call('/dev/display','drivers',{'driver':driver})['drivers']
        profile=next((p for p in profiles if p['id']==driver),None)
        if not profile or not profile.get('streaming'):raise ValueError('This profile requires a replayable source')
        if len(frame)!=profile['frame_bytes']:raise ValueError('Frame size does not match the selected profile')
        created=view_id is None
        sid=None;commit_sent=False
        try:
            if created:
                view_id=self.call('/sys/ui','create')['view_id']
                self.last_view_id=view_id
                self.call('/sys/ui','activate',{'view_id':view_id})
            else:self.last_view_id=view_id
            r=self.call('/dev/display','begin',{'driver':driver,'force':force,'view_id':view_id})
            sid=r['stream_id'];self.last_stream_id=sid;self.last_job_id=r['job_id']
            deadline=time.monotonic()+self.timeout
            while True:
                state=self.call('/dev/display','read')
                if state.get('stream_id')!=sid:raise RuntimeError('Display stream changed before upload')
                if state.get('receiving'):break
                if state.get('state') in ('fault','failed'):raise RuntimeError(state.get('error','display preparation failed'))
                if time.monotonic()>=deadline:raise TimeoutError('Display preparation timed out')
                time.sleep(.02)
            chunk_size=min(1024,r['chunk_limit'])
            if chunk_size<=0:raise ValueError('Invalid display chunk limit')
            for offset in range(0,len(frame),chunk_size):
                chunk=frame[offset:offset+chunk_size]
                reply=self.call('/dev/display','chunk',{'stream_id':sid,'offset':offset,'hex':chunk.hex()})
                if reply.get('next_offset')!=offset+len(chunk):raise IOError('Display did not acknowledge the complete chunk')
            commit_sent=True
            return self.call('/dev/display','commit',{'stream_id':sid,'sha256':hashlib.sha256(frame).hexdigest()})
        except Exception as error:
            # A missing commit reply is an unknown outcome, not permission to
            # discard the view or interrupt hardware that may already be running.
            if commit_sent and not isinstance(error,RpcError):
                if hasattr(error,'add_note'):error.add_note(f'Commit outcome unknown; inspect job {self.last_job_id}, view {view_id} before retrying.')
                raise
            cleanup=[]
            if sid is not None:cleanup.append(('/dev/display','abort',{'stream_id':sid}))
            if created and view_id is not None:cleanup.append(('/sys/ui','close',{'view_id':view_id}))
            for path,method,args in cleanup:
                try:self.call(path,method,args)
                except Exception as failure:
                    if hasattr(error,'add_note'):error.add_note(f'{method} cleanup failed: {failure}')
            raise

def main():
    p=argparse.ArgumentParser(description=__doc__)
    t=p.add_mutually_exclusive_group(required=True);t.add_argument('--url');t.add_argument('--port')
    p.add_argument('path');p.add_argument('method',nargs='?',default='read');p.add_argument('--args',default='{}')
    p.add_argument('--publish-ui',type=Path);p.add_argument('--mount',default='/storage/user');p.add_argument('--entry',default='ui.json');p.add_argument('--native',type=Path);p.add_argument('--driver');p.add_argument('--force',action='store_true')
    a=p.parse_args()
    if a.native and not a.driver:p.error('--native requires --driver from device discovery')
    c=Client(a.url,a.port)
    result=c.publish_ui(a.publish_ui,a.mount,a.entry,a.driver) if a.publish_ui else c.present_native(a.native.read_bytes(),a.driver,a.force) if a.native else c.call(a.path,a.method,json.loads(a.args))
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
