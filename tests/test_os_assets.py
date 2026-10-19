import importlib.util, json, struct, tempfile, unittest
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
    def test_factory_action_icons_visible_bounds(self):
        for path in (ROOT/'hardware/agent-os/runtime/lckfb-suite/assets/icons').glob('*.png'):
            im=Image.open(path).convert('RGBA');box=im.getchannel('A').getbbox()
            self.assertIsNotNone(box,str(path))
            self.assertLessEqual(abs(box[0]+box[2]-im.width),1,str(path))
            self.assertLessEqual(abs(box[1]+box[3]-im.height),1,str(path))
    def test_factory_lcd_text_contrast_after_rgb565(self):
        # This verifies actual numeric output colors. Physical panel quality is
        # separately checked on hardware, not inferred from this calculation.
        def rgb(value):
            n=int(value.lstrip('#'),16)
            return [round(((n>>16)&255)/255*31)/31,
                    round(((n>>8)&255)/255*63)/63,round((n&255)/255*31)/31]
        def luminance(color):
            return sum(w*(c/12.92 if c<=.04045 else ((c+.055)/1.055)**2.4)
                       for w,c in zip((.2126,.7152,.0722),color))
        def visit(node,bg,fg,path):
            style=node.get('style',{})
            if 'bgColor' in style and style.get('bgOpacity',255)==255:bg=style['bgColor']
            fg=style.get('textColor',fg)
            if node['type']=='label':
                a,b=sorted((luminance(rgb(bg)),luminance(rgb(fg))))
                self.assertGreaterEqual((b+.05)/(a+.05),4.5,(str(path),node.get('id'),bg,fg))
            for child in node.get('children',[]):visit(child,bg,fg,path)
        for suite in ('lckfb-suite','xingzhi-cube-suite'):
            for path in (ROOT/'hardware/agent-os/runtime'/suite/'assets/ui').glob('*.json'):
                for node in json.loads(path.read_text())['assets']:visit(node,'#ffffff','#000000',path)
if __name__=='__main__':unittest.main()
