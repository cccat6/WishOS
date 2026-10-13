import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('board_pack',ROOT/'tools/prepare_board_package.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
class PackageTests(unittest.TestCase):
 def fixture(self,root):
  b=json.loads((pack.DEFAULT/'board.json').read_text());b['port']=str((pack.DEFAULT/b['port']).resolve())
  b['assets']={'/ui/home.json':'home.json','/default-app.json':'app.json'}
  b['startup']={}
  (root/'home.json').write_text('{"version":"0.1.1","label":"可爱 🐱"}')
  (root/'app.json').write_text('{"format":"notes-app/1","entry":"ui/home.json"}')
  return b
 def write(self,root,b): (root/'board.json').write_text(json.dumps(b))
 def test_default_and_wiring_change(self):
  pack.load(pack.DEFAULT)
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);b['id']='custom-board';b['pins']['MOSI'],b['pins']['SCLK']=b['pins']['SCLK'],b['pins']['MOSI'];self.write(root,b)
   pack.generate(root,root/'generated');s=(root/'generated/BoardPackage.h').read_text();self.assertIn('MOSI=18',s);self.assertIn('custom-board',s)
 def test_conflicts_and_capability_constraints(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);b['pins']['MOSI']=b['pins']['SCLK'];self.write(root,b)
   with self.assertRaisesRegex(ValueError,'conflicting'):pack.load(root)
   b=self.fixture(root);b['pins']['KEY_A'],b['pins']['MIC_WS']=b['pins']['MIC_WS'],b['pins']['KEY_A'];self.write(root,b)
   with self.assertRaisesRegex(ValueError,'KEY_A'):pack.load(root)
 def test_missing_assets_and_geometry(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);b['startup']['script']='/system/missing.lua';self.write(root,b)
   with self.assertRaisesRegex(ValueError,'startup script'):pack.load(root)
   b=self.fixture(root);b['display']['width']=240;self.write(root,b)
   with self.assertRaisesRegex(ValueError,'geometry'):pack.load(root)
 def test_factory_is_only_a_runtime_reference(self):
  _,board,_,_,assets=pack.load(pack.DEFAULT)
  self.assertLessEqual(set(board['startup']),{'script','memory_bytes'})
  self.assertIn(board['startup']['script'][7:],assets)
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root)
   b['assets']['/custom.lua']='custom.lua';(root/'custom.lua').write_text('function on_start() end')
   b['startup']={'script':'/system/custom.lua'};self.write(root,b)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text()
   self.assertIn('STARTUP_SCRIPT[]="/system/custom.lua"',text);self.assertNotIn('STARTUP_UI',text)
   b['startup']['memory_bytes']=48000;self.write(root,b);pack.generate(root,root/'generated');self.assertIn('STARTUP_MEMORY_BYTES=48000',(root/'generated/BoardPackage.h').read_text())
   b['startup']['memory_bytes']='48000';self.write(root,b)
   with self.assertRaisesRegex(ValueError,'memory ceiling'):pack.load(root)
   b['startup']={};self.write(root,b);pack.load(root)
 def test_binary_assets_preserve_nul_and_size(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);b['assets']['/icons/test.bin']='test.bin';raw=b'\x00\xff\x00data';(root/'test.bin').write_bytes(raw);self.write(root,b)
   *_,assets=pack.load(root);self.assertEqual(assets['/icons/test.bin'],raw)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text();self.assertIn('\\x00\\xff\\x00\\x64\\x61\\x74\\x61',text)
 def test_different_port_does_not_require_e6_pins_or_three_buttons(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);port=root/'port';port.mkdir();(port/'Port.inc').write_text('// test port, not firmware\n')
   (port/'port.json').write_text(json.dumps({'format':'notes-port/1','id':'test','mcu':'test-dual-core','entry':'Port.inc','display':{'width':240,'height':280},'required_pins':['KEY','TOUCH_SDA','TOUCH_SCL'],'available_gpios':[1,2,3],'button_count':1}))
   b.update(port='port',mcu='test-dual-core',pins={'KEY':1,'TOUCH_SDA':2,'TOUCH_SCL':3},hardware={},buttons=['KEY'],display={'width':240,'height':280});self.write(root,b)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text();self.assertIn('buttons[]={KEY}',text);self.assertNotIn('KEY_C',text)
if __name__=='__main__':unittest.main()
