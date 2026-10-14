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
  b=json.loads((pack.DEFAULT/'board.json').read_text());b['composition']=str((pack.DEFAULT/b['composition']).resolve());b['partition_file']='partitions.csv';(root/'partitions.csv').write_bytes((pack.DEFAULT/'partitions.csv').read_bytes())
  b.pop('runtime',None)
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
 def test_same_c6_can_have_an_unrelated_display_touch_and_one_button(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);port=root/'port';port.mkdir();(port/'Port.inc').write_text('// test port, not firmware\n')
   (port/'composition.json').write_text(json.dumps({'format':'notes-composition/1','id':'test','platforms':['esp32-c6'],'entry':'Port.inc','display':{'width':240,'height':280},'required_pins':['KEY','TOUCH_SDA','TOUCH_SCL'],'available_gpios':[1,2,3],'button_count':1}))
   b.update(drivers=[],composition='port',pins={'KEY':1,'TOUCH_SDA':2,'TOUCH_SCL':3},hardware={},buttons=['KEY'],display={'width':240,'height':280});self.write(root,b)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text();self.assertIn('buttons[]={KEY}',text);self.assertNotIn('KEY_C',text)
 def test_hardware_values_follow_composition_schema_not_global_positive_range(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);port=root/'composition';port.mkdir();(port/'Port.inc').write_text('// fixture')
   schema={'offset':{'type':'number','minimum':-10,'maximum':10},'disabled_level':{'type':'integer','enum':[0,1]},'clock_hz':{'type':'integer','maximum':80000000},'model':{'type':'string'},'addresses':{'type':'array'}}
   (port/'composition.json').write_text(json.dumps({'format':'notes-composition/1','id':'fixture','platforms':['esp32-c6'],'entry':'Port.inc','required_pins':[],'hardware_schema':schema,'button_count':0,'display':None}))
   b.update(composition='composition',drivers=[],pins={},buttons=[],display=None,hardware={'offset':-1.25,'disabled_level':0,'clock_hz':40000000,'model':'custom device','addresses':[24,25]});self.write(root,b)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text();self.assertIn('OFFSET=-1.25',text);self.assertIn('DISABLED_LEVEL=0',text);self.assertIn('CLOCK_HZ=40000000',text);self.assertIn('HARDWARE_JSON',text)
   b['hardware']['clock_hz']=80000001;self.write(root,b)
   with self.assertRaisesRegex(ValueError,'maximum'):pack.load(root)
 def test_explicit_signal_aliases_are_device_constraints(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);port=root/'composition';port.mkdir();(port/'Port.inc').write_text('// fixture')
   p={'format':'notes-composition/1','id':'fixture','platforms':['esp32-c6'],'entry':'Port.inc','required_pins':['SDA','SECOND_SDA'],'hardware_schema':{},'button_count':0,'display':None}
   (port/'composition.json').write_text(json.dumps(p));b.update(composition='composition',drivers=[],pins={'SDA':6,'SECOND_SDA':6},buttons=[],display=None,hardware={});self.write(root,b)
   with self.assertRaisesRegex(ValueError,'conflicting'):pack.load(root)
   p['shared_pin_groups']=[['SDA','SECOND_SDA']];(port/'composition.json').write_text(json.dumps(p));pack.load(root)
if __name__=='__main__':unittest.main()
