#!/usr/bin/env python3
"""Bounded Notes hardware ABI client over HTTP or USB CDC; no Flash cache."""
from __future__ import annotations
import argparse, hashlib, json, time, urllib.request, urllib.error, uuid, subprocess
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
    def publish_app(self, directory, mount='/storage/user', entry='app.json', *, persist=False, launch=True, launcher=False):
        """Verify an immutable package, register its descriptor, then optionally launch.

        Persistence retains the installation reference; it does not silently replace
        the launcher. A lost activation reply is reconciled, never replayed.
        """
        directory=Path(directory).resolve();relative=Path(entry)
        if relative.is_absolute() or '..' in relative.parts or not (directory/relative).is_file():
            raise ValueError('Descriptor must be a file within the package')
        descriptor=json.loads((directory/relative).read_text(encoding='utf-8'))
        if descriptor.get('format')!='wishos-app/1' or not isinstance(descriptor.get('id'),str) or not descriptor.get('title'):
            raise ValueError('A wishos-app/1 descriptor with id, title and entry is required')
        local_entry=(directory/relative.parent/descriptor['entry']).resolve()
        if not local_entry.is_relative_to(directory) or not local_entry.is_file():
            raise ValueError('Verified publication requires an entry inside the package')
        assets=[]
        for path in sorted(directory.rglob('*')):
            if path.is_symlink():raise ValueError('Symlinks are not package assets')
            name=path.relative_to(directory).as_posix()
            if any(c in name for c in ('\\','?','#')):raise ValueError('Invalid resource path')
            if path.is_file():
                if name in ('package.json','files.jsonl'):raise ValueError('Package metadata is generated by the publisher')
                digest_state=hashlib.sha256()
                with path.open('rb') as source:
                    for chunk in iter(lambda:source.read(65536),b''):digest_state.update(chunk)
                digest=digest_state.hexdigest()
                assets.append((path,name,path.stat().st_size,digest))
        runtime=self.call('/sys/capabilities','describe',{'path':'/sys/runtime'})
        source_limit=runtime.get('entry_source_limit_bytes')
        if not isinstance(source_limit,int):raise ValueError('Target must advertise its Lua source limit')
        for required in descriptor.get('requires',[]):
            info=self.call('/sys/capabilities','describe',{'path':required['path']})
            methods=set(info.get('methods','').split(','))
            if not set(required['methods'])<=methods:raise ValueError('Missing application capability: '+required['path'])
        for field in ('memory_bytes','instruction_budget','system_call_budget'):
            value=descriptor.get(field)
            if value is None:continue
            bound=runtime.get('budgets',{}).get(field)
            if not bound or type(value) is not int or not bound['minimum']<=value<=bound['maximum']:raise ValueError('Unsupported application budget: '+field)
        # Compile all Lua before any device-side mutation. The helper links the
        # same vendored compiler as the terminal and never executes the chunk.
        compiler=Path(__file__).resolve().parents[1]/'build/os-ui/notes_lua_syntax'
        if any(local.suffix=='.lua' for local,_,_,_ in assets):
            if not compiler.is_file():raise ValueError('Build the Lua checker first: cmake --build build/os-ui --target notes_lua_syntax')
            for local,name,size,_ in assets:
                if local.suffix=='.lua':
                    if not size or size>source_limit or b'\0' in local.read_bytes():raise ValueError('Lua source exceeds runtime file/type limit: '+name)
                    checked=subprocess.run([str(compiler),str(local)],text=True,capture_output=True)
                    if checked.returncode:raise ValueError(checked.stderr.strip() or 'Lua syntax rejected: '+name)
        for local,name,_,_ in assets:
            if local.suffix=='.json':json.loads(local.read_text(encoding='utf-8'))
        inventory=''.join(json.dumps({'path':name,'bytes':str(size),'sha256':digest},ensure_ascii=False,separators=(',',':'))+'\n' for _,name,size,digest in assets).encode()
        if any(len(line)>1024 for line in inventory.splitlines()):raise ValueError('Package inventory line exceeds limit')
        prior=self.call('/sys/apps','read')
        root=mount.rstrip('/')+'/app-'+uuid.uuid4().hex[:12]
        if any(len(root+'/'+name)>=256 for _,name,_,_ in assets):raise ValueError('Package path exceeds device path limit')
        if persist:
            providers=[];cursor=0
            while True:
                page=self.call('/fs','mounts',{'cursor':cursor,'limit':4});providers.extend(page['providers'])
                if not page.get('more'):break
                cursor=page['next_cursor']
            provider=next((p for p in providers if p['mount']==mount.rstrip('/')),None)
            if not provider or not provider['persistent']:raise ValueError('Persistent installation requires persistent storage')
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
        manifest={'format':'wishos-package/1','entry':relative.as_posix(),'inventory':'files.jsonl','inventory_sha256':hashlib.sha256(inventory).hexdigest()}
        header=json.dumps(manifest,ensure_ascii=False,separators=(',',':')).encode()
        for filename,payload in [('files.jsonl',inventory),('package.json',header)]:
            for offset in range(0,len(payload),1024):
                piece=payload[offset:offset+1024]
                result=self.call('/fs','write',{'file':root+'/'+filename,'offset':str(offset),'hex':piece.hex(),'truncate':offset==0})
                if result.get('written')!=len(piece):raise IOError('Incomplete package manifest')
        descriptor_path=root+'/'+relative.as_posix();app_id=descriptor['id'];target=root+'/'+local_entry.relative_to(directory).as_posix()
        admitted=self.call('/sys/apps','validate',{'manifest':descriptor_path,'package':root+'/package.json'})
        validation=admitted['operation_id'];deadline=time.monotonic()+getattr(self,'timeout',10)
        while True:
            checked=self.call('/sys/apps','validation',{'operation_id':validation})
            if checked['state']=='validated':break
            if checked['state']=='failed' or time.monotonic()>=deadline:
                self.call('/sys/apps','release_validation',{'operation_id':validation})
                raise ValueError('Native package validation failed: '+checked.get('error','timeout'))
            time.sleep(.02)
        try:self.call('/sys/apps','install',{'manifest':descriptor_path,'package':root+'/package.json','validation_id':validation,'persist':persist,'expected_revision':prior['revision']})
        except RpcError:
            self.call('/sys/apps','release_validation',{'operation_id':validation})
            raise
        except (OSError,TimeoutError):
            cursor=0;found=False
            while True:
                page=self.call('/sys/apps','list',{'cursor':cursor,'limit':4})
                found|=any(a['id']==app_id and a['manifest']==descriptor_path for a in page['applications'])
                if not page['more']:break
                cursor=page['next_cursor']
            if not found:raise RuntimeError('Installation outcome unknown; inspect application catalog before retrying')
        active=None
        if launch:
            try:self.call('/sys/apps','launch',{'id':app_id})
            except RpcError:raise
            except (OSError,TimeoutError):pass
            deadline=time.monotonic()+getattr(self,'timeout',10)
            while True:
                active=self.call('/sys/runtime','read');state=self.call('/sys/apps','read')
                if active.get('loaded') and active.get('file')==target and not active.get('fault'):break
                if not state.get('pending') or time.monotonic()>=deadline:
                    raise RuntimeError('Application activation outcome changed or failed: '+str(state.get('last_error','')))
                time.sleep(.05)
        if launcher:
            state=self.call('/sys/apps','read')
            try:self.call('/sys/apps','configure',{'launcher':app_id,'persist':persist,'expected_revision':state['revision']})
            except RpcError:raise
            except (OSError,TimeoutError):
                state=self.call('/sys/apps','read')
                if state.get('launcher')!=app_id or (persist and not state.get('persisted')):raise RuntimeError('Launcher selection outcome unknown')
        return {'root':root,'application_id':app_id,'manifest':descriptor_path,'entry':target,'verified_files':len(assets),'persisted':persist,'launcher_selected':launcher,'runtime':active}

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
    p.add_argument('path',nargs='?',default='/sys/apps');p.add_argument('method',nargs='?',default='read');p.add_argument('--args',default='{}')
    p.add_argument('--publish-ui',type=Path);p.add_argument('--mount',default='/storage/user');p.add_argument('--entry',default='ui.json');p.add_argument('--native',type=Path);p.add_argument('--driver');p.add_argument('--force',action='store_true')
    p.add_argument('--publish-app',type=Path);p.add_argument('--descriptor',default='app.json');p.add_argument('--persist',action='store_true');p.add_argument('--launcher',action='store_true')
    a=p.parse_args()
    if a.native and not a.driver:p.error('--native requires --driver from device discovery')
    c=Client(a.url,a.port)
    result=c.publish_app(a.publish_app,a.mount,a.descriptor,persist=a.persist,launcher=a.launcher) if a.publish_app else c.publish_ui(a.publish_ui,a.mount,a.entry,a.driver) if a.publish_ui else c.present_native(a.native.read_bytes(),a.driver,a.force) if a.native else c.call(a.path,a.method,json.loads(a.args))
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
