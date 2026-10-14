import importlib.util, json, unittest, tempfile
from unittest.mock import patch
from pathlib import Path
spec=importlib.util.spec_from_file_location('os_client',Path(__file__).parents[1]/'tools/os_client.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Fake(m.Client):
    def __init__(self):self.calls=[];self.timeout=1;self.frame_bytes=172800;self.streaming=True;self.chunk_limit=1024
    def call(self,path,method='read',args=None,request_id=None):
        self.calls.append((path,method,args))
        if method=='drivers':return {'drivers':[{'id':args['driver'],'frame_bytes':self.frame_bytes,'streaming':self.streaming}]}
        if method=='create':return {'view_id':1}
        if method=='read':return {'receiving':True,'stream_id':3}
        if method in ('open','begin'):return {'stream_id':3,'job_id':7,'chunk_limit':self.chunk_limit}
        if method=='chunk':
            size=len(json.dumps({'id':'x'*32,'path':path,'method':method,'args':args}).encode());assert size<=4096
            return {'next_offset':args['offset']+len(args['hex'])//2}
        return {'status':'queued','job_id':7}
class Tests(unittest.TestCase):
    def test_stream_exact(self):
        c=Fake();r=c.present_native(bytes([0x11])*172800,'e6.ink6.image-v1',True);self.assertEqual(r['job_id'],7)
        chunks=[x[2] for x in c.calls if x[1]=='chunk'];self.assertEqual(sum(len(x['hex'])//2 for x in chunks),172800);self.assertEqual([x['offset'] for x in chunks],list(range(0,172800,1024)));self.assertEqual(c.calls[-1][1],'commit')
    def test_bad_frame_has_no_mutating_io(self):
        c=Fake()
        with self.assertRaises(ValueError):c.present_native(b'x','e6.ink6.ui-v1')
        self.assertTrue(all(m=='drivers' for _,m,_ in c.calls))
        c.streaming=False
        with self.assertRaisesRegex(ValueError,'replayable'):c.present_native(bytes(172800),'e6.color-fast.v1')
        self.assertTrue(all(m=='drivers' for _,m,_ in c.calls))
    def test_other_display_geometry_and_format(self):
        c=Fake();c.frame_bytes=8*8*2;c.chunk_limit=32
        c.present_native(bytes(range(128)),'lcd.rgb565')
        chunks=[args for _,method,args in c.calls if method=='chunk']
        self.assertEqual([a['offset'] for a in chunks],[0,32,64,96])
        self.assertEqual(b''.join(bytes.fromhex(a['hex']) for a in chunks),bytes(range(128)))
    def test_publish_only_after_all_files(self):
        class Files(Fake):
            def call(self,path,method='read',args=None,request_id=None):
                self.calls.append((path,method,args))
                if method=='write':return {'written':len(args['hex'])//2}
                return {'job_id':2,'status':'queued'}
        c=Files()
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp);(p/'ui.json').write_text('{}');(p/'assets').mkdir();(p/'assets'/'x.bin').write_bytes(bytes(2500))
            result=c.publish_ui(p,'/storage/ram')
        self.assertEqual(c.calls[-1][1],'open');self.assertEqual(c.calls[-1][2]['history'],'replace')
        self.assertEqual(sum(len(a['hex'])//2 for _,m,a in c.calls if m=='write'),2502)
        self.assertTrue(result['root'].startswith('/storage/ram/app-'))
    def test_preparation_and_upload_failure_release_created_view(self):
        for failure_method in ('activate','begin','read','chunk'):
            class Failing(Fake):
                def call(self,path,method='read',args=None,request_id=None):
                    if method==failure_method:
                        self.calls.append((path,method,args));raise RuntimeError('injected failure')
                    return super().call(path,method,args,request_id)
            c=Failing();c.frame_bytes=128
            with self.assertRaisesRegex(RuntimeError,'injected failure'):c.present_native(bytes(128),'test')
            self.assertEqual(c.calls[-1][1],'close')
            if failure_method in ('read','chunk'):self.assertEqual(c.calls[-2][1],'abort')
    def test_wait_timeout_and_existing_view(self):
        class Waiting(Fake):
            def call(self,path,method='read',args=None,request_id=None):
                result=super().call(path,method,args,request_id)
                if method=='read':return {'stream_id':3,'receiving':False}
                return result
        c=Waiting();c.frame_bytes=128
        with patch.object(m.time,'monotonic',side_effect=[0,2]):
            with self.assertRaises(TimeoutError):c.present_native(bytes(128),'test',view_id=44)
        self.assertEqual(c.calls[-1][1],'abort');self.assertFalse(any(method=='close' for _,method,_ in c.calls))
    def test_commit_unknown_keeps_receipt_context(self):
        class LostCommit(Fake):
            def call(self,path,method='read',args=None,request_id=None):
                result=super().call(path,method,args,request_id)
                if method=='commit':raise TimeoutError('reply lost')
                return result
        c=LostCommit();c.frame_bytes=128
        with self.assertRaises(TimeoutError):c.present_native(bytes(128),'test')
        self.assertEqual(c.calls[-1][1],'commit');self.assertEqual(c.last_job_id,7);self.assertEqual(c.last_stream_id,3)
    def test_short_chunk_and_commit_rejection_cleanup(self):
        for fail in ('chunk','commit'):
            class Rejected(Fake):
                def call(self,path,method='read',args=None,request_id=None):
                    result=super().call(path,method,args,request_id)
                    if method==fail:
                        if method=='chunk':return {'next_offset':1}
                        raise m.RpcError('hash_mismatch')
                    return result
            c=Rejected();c.frame_bytes=128
            with self.assertRaises((IOError,m.RpcError)):c.present_native(bytes(128),'test')
            self.assertEqual([method for _,method,_ in c.calls][-2:],['abort','close'])
    def test_public_native_driver(self):
        c=Fake();c.present_native(bytes([0x11])*172800,'e6.ink6.ui-v1')
        begin=next(a for _,m,a in c.calls if m=='begin');self.assertEqual(begin['driver'],'e6.ink6.ui-v1');self.assertEqual(begin['view_id'],1)
        self.assertFalse(hasattr(c,'wave'))
if __name__=='__main__':unittest.main()
