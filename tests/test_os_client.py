import importlib.util, json, unittest, tempfile
from pathlib import Path
spec=importlib.util.spec_from_file_location('os_client',Path(__file__).parents[1]/'tools/os_client.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Fake(m.Client):
    def __init__(self):self.calls=[];self.timeout=1;self.frame_bytes=172800;self.streaming=True;self.chunk_limit=1024
    def call(self,path,method='read',args=None,request_id=None):
        self.calls.append((path,method,args))
        if method=='drivers':return {'drivers':[{'id':args['driver'],'frame_bytes':self.frame_bytes,'streaming':self.streaming}]}
        if method=='create':return {'view_id':1}
        if method=='read':return {'receiving':True}
        if method in ('open','begin'):return {'stream_id':3,'chunk_limit':self.chunk_limit}
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
    def test_public_native_driver(self):
        c=Fake();c.present_native(bytes([0x11])*172800,'e6.ink6.ui-v1')
        begin=next(a for _,m,a in c.calls if m=='begin');self.assertEqual(begin['driver'],'e6.ink6.ui-v1');self.assertEqual(begin['view_id'],1)
        self.assertFalse(hasattr(c,'wave'))
if __name__=='__main__':unittest.main()
