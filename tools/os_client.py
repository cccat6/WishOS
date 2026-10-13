#!/usr/bin/env python3
"""Bounded Notes hardware ABI client over HTTP or USB CDC; no Flash cache."""
from __future__ import annotations
import argparse, hashlib, json, time, urllib.request, urllib.error, uuid
from pathlib import Path

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
        if not reply.get('ok'): raise RuntimeError(reply.get('error',reply))
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
    def present_native(self,frame:bytes,driver='e6.ink6.ui-v1',force=False,view_id=None):
        if len(frame)!=172800: raise ValueError('Expected raw native720x480 4bpp (172800 bytes)')
        inks={0,1,2,3,5,6}
        if any(x>>4 not in inks or x&15 not in inks for x in frame): raise ValueError('Invalid native six-color ink code')
        created=view_id is None
        if created:
            view_id=self.call('/sys/ui','create')['view_id']
            self.call('/sys/ui','activate',{'view_id':view_id})
        self.last_view_id=view_id
        try:r=self.call('/dev/display','begin',{'driver':driver,'force':force,'view_id':view_id})
        except RuntimeError:
            if created:self.call('/sys/ui','close',{'view_id':view_id})
            raise
        sid=r['stream_id']
        deadline=time.monotonic()+self.timeout
        while True:
            state=self.call('/dev/display','read')
            if state.get('receiving'):break
            if state.get('state') in ('fault','failed'):raise RuntimeError(state.get('error','display preparation failed'))
            if time.monotonic()>=deadline:raise TimeoutError('Display preparation timed out; inspect job before retrying')
            time.sleep(.02)
        try:
            for offset in range(0,len(frame),1024):
                chunk=frame[offset:offset+1024]; self.call('/dev/display','chunk',{'stream_id':sid,'offset':offset,'hex':chunk.hex()})
            return self.call('/dev/display','commit',{'stream_id':sid,'sha256':hashlib.sha256(frame).hexdigest()})
        except Exception:
            # Before commit, abort only this stream. Never abort an active waveform.
            try: self.call('/dev/display','abort',{'stream_id':sid})
            except Exception: pass
            raise

def main():
    p=argparse.ArgumentParser(description=__doc__)
    t=p.add_mutually_exclusive_group(required=True);t.add_argument('--url');t.add_argument('--port')
    p.add_argument('path');p.add_argument('method',nargs='?',default='read');p.add_argument('--args',default='{}')
    p.add_argument('--publish-ui',type=Path);p.add_argument('--mount',default='/storage/user');p.add_argument('--entry',default='ui.json');p.add_argument('--native',type=Path);p.add_argument('--driver',default='e6.ink6.ui-v1');p.add_argument('--force',action='store_true')
    a=p.parse_args();c=Client(a.url,a.port)
    result=c.publish_ui(a.publish_ui,a.mount,a.entry,a.driver) if a.publish_ui else c.present_native(a.native.read_bytes(),a.driver,a.force) if a.native else c.call(a.path,a.method,json.loads(a.args))
    print(json.dumps(result,ensure_ascii=False,indent=2))
if __name__=='__main__':main()
