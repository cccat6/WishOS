import importlib.util
import json
import tempfile
import unittest
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('board_pack',ROOT/'tools/prepare_board_package.py')
pack=importlib.util.module_from_spec(spec);spec.loader.exec_module(pack)
pack.DEFAULT=ROOT/'tests/fixtures/device-template'
class PackageTests(unittest.TestCase):
 def fixture(self,root):
  b=json.loads((pack.DEFAULT/'board.json').read_text());b['composition']=str((pack.DEFAULT/b['composition']).resolve());b['partition_file']='partitions.csv';(root/'partitions.csv').write_bytes((pack.DEFAULT/'partitions.csv').read_bytes())
  b.pop('runtime',None)
  b['assets']={'/ui/home.json':'home.json','/default-app.json':'app.json'}
  (root/'home.json').write_text('{"version":"0.1.1","label":"可爱 🐱"}')
  (root/'app.json').write_text('{"format":"notes-app/1","entry":"ui/home.json"}')
  return b
 def write(self,root,b): (root/'board.json').write_text(json.dumps(b))
 def test_headless_initial_files_are_separate_from_recovery(self):
  root,b,_,_,assets=pack.load(ROOT/'hardware/agent-os/boards/esp32-c3-runtime')
  compiled=pack.compiled_assets(root,b,assets)
  self.assertIn('/control/control.js.gz',assets)
  self.assertNotIn('/control/control.js.gz',compiled)
  self.assertNotIn('/apps/launcher.lua',compiled)
  self.assertIn('/recovery/launcher.lua',compiled)
  self.assertIn('/lib/navigation.lua',compiled)
  app=json.loads(assets['/applications.json'])['applications'][0]
  self.assertEqual(app['fallback_manifest'],'/system/recovery/launcher.json')
 def test_factory_ui_documents_are_in_the_effective_package(self):
  for board_id in ('lckfb-szpi-esp32s3','xingzhi-cube-154tft-wifi'):
   root,board,_,_,assets=pack.load(pack.HARDWARE/'boards'/board_id)
   directory,_=pack.runtime_package(root,board)
   for path in (directory/'assets/ui').glob('*.json'):
    self.assertIn('/ui/'+path.name,assets,(board_id,path.name))

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
   root=Path(folder);b=self.fixture(root);b['assets']['/bad.lua']='missing.lua';self.write(root,b)
   with self.assertRaises((ValueError,FileNotFoundError)):pack.load(root)
   b=self.fixture(root);b['display']['width']=240;self.write(root,b)
   with self.assertRaisesRegex(ValueError,'geometry'):pack.load(root)
 def test_all_lua_assets_obey_the_runtime_source_contract(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);board=self.fixture(root);board['assets']['/suite/helper.lua']='helper.lua';self.write(root,board)
   (root/'helper.lua').write_text('--'+('x'*8190));pack.load(root)
   (root/'helper.lua').write_text('--'+('x'*8191))
   with self.assertRaisesRegex(ValueError,'runtime Lua asset'):pack.load(root)
   (root/'helper.lua').write_text('')
   with self.assertRaisesRegex(ValueError,'runtime Lua asset'):pack.load(root)

 def test_factory_is_only_a_runtime_reference(self):
  _,board,_,_,assets=pack.load(pack.DEFAULT)
  catalog=json.loads(assets['/applications.json']);self.assertEqual(catalog['launcher'],'launcher')
  for application in catalog['applications']:
   descriptor=json.loads(assets[application['manifest'][7:]]);self.assertEqual(descriptor['format'],'wishos-app/1');self.assertEqual(descriptor['id'],application['id']);self.assertTrue(descriptor['source']['repository'])
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root)
   b['assets']['/custom.lua']='custom.lua';(root/'custom.lua').write_text('function on_start() end')
   descriptor={'format':'wishos-app/1','id':'custom','title':'Custom','entry':'custom.lua'}
   (root/'descriptor.json').write_text(json.dumps(descriptor));b['assets']['/app.json']='descriptor.json'
   catalog={'launcher':'custom','applications':[{'id':'custom','title':'Custom','manifest':'/system/app.json'}]}
   (root/'applications.json').write_text(json.dumps(catalog));b['assets']['/applications.json']='applications.json';self.write(root,b)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text();self.assertNotIn('STARTUP_SCRIPT',text);self.assertIn('/applications.json',text)
   descriptor['entry']='missing.lua';(root/'descriptor.json').write_text(json.dumps(descriptor))
   with self.assertRaisesRegex(ValueError,'application entry'):pack.load(root)
   descriptor['entry']='custom.lua';(root/'descriptor.json').write_text(json.dumps(descriptor));catalog['applications'].append(catalog['applications'][0]);(root/'applications.json').write_text(json.dumps(catalog))
   with self.assertRaisesRegex(ValueError,'duplicate'):pack.load(root)
 def test_browser_assets_are_compressed_without_changing_source(self):
  import gzip
  _,_,_,_,assets=pack.load(pack.DEFAULT)
  for path in ('/control/setup.html','/control/control.js','/control/display.js','/control/locale.js'):
   source=pack.asset_sources(pack.DEFAULT,pack.read_json(pack.DEFAULT/'board.json'))[path]
   self.assertEqual(gzip.decompress(assets[path+'.gz']),source.read_bytes())
   self.assertNotIn(path,assets)

 def test_component_index_describes_selected_layers_and_custom_hardware(self):
  root,board,_,port,_=pack.load(pack.DEFAULT)
  index=pack.component_index(root,board,port)
  self.assertEqual(index['format'],'wishos-components/1')
  self.assertEqual({c['layer'] for c in index['components']},{'system','driver','application'})
  compiled={c['id'] for c in index['components'] if c['kind']=='peripheral'}
  self.assertEqual(compiled,set(pack.driver_packages(board,port)))
  with tempfile.TemporaryDirectory() as folder:
   target=Path(folder);pack.generate(pack.DEFAULT,target)
   self.assertIn('/components.json',(target/'BoardPackage.h').read_text())

 def test_generated_metadata_obeys_the_same_asset_budget(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);board=self.fixture(root);original=Path(board['composition']);port=root/'composition';port.mkdir();spec=pack.read_json(original/'composition.json');spec['entry']='Port.inc';spec['asset_budget_bytes']=500
   (port/'Port.inc').write_text('// validation fixture');(port/'composition.json').write_text(json.dumps(spec));board['composition']='composition';self.write(root,board)
   with self.assertRaisesRegex(ValueError,'compiled asset budget'):pack.load(root)

 def test_internal_filesystem_binding_follows_device_configuration(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root)
   b['runtime_filesystem'].update(partition='custom_data',mount='/storage/custom',native_mount='/customfs')
   (root/'partitions.csv').write_text((root/'partitions.csv').read_text().replace('spiffs,data,spiffs','custom_data,data,spiffs'))
   self.write(root,b);pack.generate(root,root/'generated')
   text=(root/'generated/BoardPackage.h').read_text()
   self.assertIn('FLASH_PARTITION[]=\"custom_data\"',text)
   self.assertIn('FLASH_MOUNT[]=\"/storage/custom\"',text)
   self.assertIn('FLASH_NATIVE_MOUNT[]=\"/customfs\"',text)
   self.assertIn('RUNTIME_PATH[]=\"/storage/custom/factory\"',text)

 def test_binary_assets_preserve_nul_and_size(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);b['assets']['/icons/test.bin']='test.bin';raw=b'\x00\xff\x00data';(root/'test.bin').write_bytes(raw);self.write(root,b)
   *_,assets=pack.load(root);self.assertEqual(assets['/icons/test.bin'],raw)
   pack.generate(root,root/'generated');text=(root/'generated/BoardPackage.h').read_text();self.assertIn('\\x00\\xff\\x00\\x64\\x61\\x74\\x61',text)
 def test_xingzhi_effective_quota_covers_factory_files_and_user_room(self):
  _,board,_,_,assets=pack.load(pack.HARDWARE/'boards/xingzhi-cube-154tft-wifi')
  services=json.loads(assets['/services.json'])
  flash=next(t for t in services['downloads']['targets'] if t['provider']=='flash')
  # The public transfer contract measures total volume use, including factory fonts.
  stored=sum(len(v.encode()) if isinstance(v,str) else len(v) for k,v in assets.items() if k not in ('/components.json','/defaults.json','/applications.json','/services.json'))
  self.assertGreater(int(flash['quota_bytes']),stored+524288)
  self.assertEqual(board['application_providers'],['flash'])
 def test_xingzhi_adc_pin_must_have_adc_capability(self):
  with tempfile.TemporaryDirectory() as folder:
   source=pack.HARDWARE/'boards/xingzhi-cube-154tft-wifi';root=Path(folder)
   # Resolve the actual composition; the invalid pin is rejected before assets.
   board=json.loads((source/'board.json').read_text());board['composition']=str((source/board['composition']).resolve());board.pop('runtime');board['partition_file']='partitions.csv';(root/'partitions.csv').write_bytes((source/'partitions.csv').read_bytes());board['assets']={};board['pins']['BATTERY_ADC']=42
   (root/'board.json').write_text(json.dumps(board))
   with self.assertRaisesRegex(ValueError,'incompatible with required hardware capability'):pack.load(root)
 def test_lckfb_effective_services_admit_camera_temporary_frames(self):
  _,board,_,_,assets=pack.load(pack.HARDWARE/'boards/lckfb-szpi-esp32s3')
  services=json.loads(assets['/services.json'])
  ram=next(v for v in services['volatile_storage'] if v['provider']=='ram')
  # The currently displayed image remains leased while its successor is copied.
  width,height=board['display']['width'],board['display']['height']
  self.assertGreaterEqual(ram['quota_bytes'],2*(width*height*2+12))
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
 def test_flash_floor_and_partition_capacity_are_checked_before_staging(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);b=self.fixture(root);b['flash_bytes']=2*1024*1024;self.write(root,b)
   with self.assertRaisesRegex(ValueError,'at least 4'):pack.load(root)
   b=self.fixture(root);b['application_bytes']+=4096;self.write(root,b)
   with self.assertRaisesRegex(ValueError,'application budget'):pack.load(root)
   b=self.fixture(root);b['fqbn']=b['fqbn'].replace('FlashSize=4M','FlashSize=8M');self.write(root,b)
   with self.assertRaisesRegex(ValueError,'capacity mismatch'):pack.load(root)
   b=self.fixture(root);(root/'partitions.csv').write_text((root/'partitions.csv').read_text().replace('0x3FF000','0x400000'));self.write(root,b)
   with self.assertRaisesRegex(ValueError,'exceeds declared'):pack.load(root)
 def test_4_8_16_mib_layouts_keep_user_storage_independent_of_kernel(self):
  source=pack.HARDWARE/'boards/esp32-c3-runtime'
  for mib in (4,8,16):
   with self.subTest(mib=mib),tempfile.TemporaryDirectory() as folder:
    root=Path(folder);b=json.loads((source/'board.json').read_text())
    for key in ('composition','runtime'):b[key]=str((source/b[key]).resolve())
    b['flash_bytes']=mib*1048576;b['fqbn']=b['fqbn'].replace('FlashSize=4M',f'FlashSize={mib}M');b['application_bytes']=0x200000
    (root/'partitions.csv').write_text(f'nvs,data,nvs,0x9000,0x5000\napp0,app,ota_0,0x10000,0x200000\nnfs_test,data,spiffs,0x210000,{hex(mib*1048576-0x210000)}\n')
    self.write(root,b);_,actual,_,_,_=pack.load(root);self.assertEqual(actual['flash_bytes'],mib*1048576);self.assertEqual(actual['application_providers'],['flash'])
if __name__=='__main__':unittest.main()
