import importlib.util, struct, tempfile, unittest
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).parents[1]
spec=importlib.util.spec_from_file_location('compiler', ROOT/'tools/compile_os_image.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class Tests(unittest.TestCase):
    def test_rgb565_and_alpha_planes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'a.png';dst=root/'a.bin'
            im=Image.new('RGBA',(2,1));im.putdata([(255,0,0,255),(0,255,0,80)]);im.save(src)
            result=m.compile_image(src,dst);data=dst.read_bytes()
            self.assertEqual(struct.unpack('<BBHHHHH',data[:12]),(0x19,0x14,0,2,1,4,0))
            self.assertEqual(data[12:],bytes([0,0xf8,0xe0,7,255,80]));self.assertEqual(result['bytes'],18)
    def test_fit_and_crop(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);src=root/'wide.png';Image.new('RGB',(16,8),'red').save(src)
            self.assertEqual(m.compile_image(src,root/'contain.bin',8,8,'contain')['format'],'RGB565A8')
            self.assertEqual(m.compile_image(src,root/'cover.bin',8,8,'cover')['format'],'RGB565')
    def test_svg_resource_compilation(self):
        with tempfile.TemporaryDirectory() as tmp:
            dst=Path(tmp)/'icon.bin';result=m.compile_image(ROOT/'examples/agent-os/dashboard/icon.svg',dst,64,64)
            self.assertTrue(result['streamable']);self.assertEqual(result['width'],64);self.assertEqual(result['height'],64)
if __name__=='__main__':unittest.main()
