import hashlib
import json
import shutil
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import wishos_catalog as catalog
import prepare_board_package as boardpack
import build_wishos as builder
import release_wishos as release
import prepare_speech as speech

class BuildTests(unittest.TestCase):
 def test_change_selection_follows_driver_and_runtime_ownership(self):
  self.assertEqual(catalog.select(['hardware/agent-os/boards/esp32-c3-runtime/board.json']),['esp32-c3-runtime'])
  self.assertEqual(catalog.select(['hardware/agent-os/drivers/e6/E6Driver.h']),['e6-nova-c6'])
  self.assertEqual(catalog.select(['hardware/agent-os/runtime/headless-suite/assets/setup.html']),['esp32-c3-runtime','esp32-s3-runtime'])
  self.assertEqual(catalog.select(['firmware/WishOS/lib/input.lua']),['e6-nova-c6','esp32-c3-runtime','esp32-s3-runtime','lckfb-szpi-esp32s3'])
  self.assertEqual(catalog.select(['firmware/WishOS/src/Core.h']),['e6-nova-c6','esp32-c3-runtime','esp32-s3-runtime','lckfb-szpi-esp32s3'])
  self.assertEqual(catalog.select(['docs/agent-os-plan.md']),[])
  self.assertEqual(catalog.select(['tools/flash_layout.py']),['e6-nova-c6','esp32-c3-runtime','esp32-s3-runtime','lckfb-szpi-esp32s3'])
  self.assertEqual(catalog.select(['hardware/agent-os/references/esp-sparkbot.json']),[])
  self.assertEqual(catalog.select(['tools/dependencies/esp-sr.json']),['e6-nova-c6'])
  self.assertEqual(catalog.select([],board='esp32-c3-runtime'),['esp32-c3-runtime'])
  with self.assertRaises(ValueError):catalog.select(board='not-a-board')

 def test_adding_a_dependency_does_not_rebuild_unrelated_boards(self):
  previous=catalog.dependency_lock()
  current=json.loads(json.dumps(previous));current['libraries']['NewSensorLibrary']={'version':'1.0.0'}
  with patch.object(catalog,'dependency_lock',return_value=current):
   self.assertEqual(catalog.select(['tools/dependencies/arduino.json'],previous_lock=previous),[])
  current=json.loads(json.dumps(previous));current['libraries']['lvgl']['version']='future'
  with patch.object(catalog,'dependency_lock',return_value=current):
   self.assertEqual(catalog.select(['tools/dependencies/arduino.json'],previous_lock=previous),['e6-nova-c6','lckfb-szpi-esp32s3'])

 def test_new_board_is_discovered_without_editing_matrix_or_kernel(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);hardware=root/'hardware/agent-os';shutil.copytree(boardpack.HARDWARE,hardware)
   new=hardware/'boards/community-c3';shutil.copytree(hardware/'boards/esp32-c3-runtime',new)
   spec=json.loads((new/'board.json').read_text());spec['id']='community-c3';spec['name']='Community wiring variant';(new/'board.json').write_text(json.dumps(spec))
   with patch.object(boardpack,'HARDWARE',hardware),patch.object(catalog,'HARDWARE',hardware),patch.object(catalog,'ROOT',root):
    self.assertEqual(catalog.select(['hardware/agent-os/boards/community-c3/board.json']),['community-c3'])
    self.assertEqual(catalog.select(['hardware/agent-os/runtime/headless-suite/assets/setup.html']),['community-c3','esp32-c3-runtime','esp32-s3-runtime'])

 def test_same_mcu_different_device_topology_has_independent_build_scope(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);hardware=root/'hardware/agent-os';shutil.copytree(boardpack.HARDWARE,hardware)
   device=hardware/'boards/community-c6-lcd';shutil.copytree(hardware/'boards/e6-nova-c6',device)
   composition=hardware/'compositions/c6-lcd-fixture';composition.mkdir()
   (composition/'Port.inc').write_text('// Host schema fixture, not an implemented LCD adapter.\n')
   (composition/'composition.json').write_text(json.dumps({'format':'notes-composition/1','id':'c6-lcd-fixture',
    'platforms':['esp32-c6'],'entry':'Port.inc','drivers':[],'required_pins':['BUTTON','LCD_CLK','LCD_DATA','TOUCH_IRQ'],
    'hardware_fields':[],'button_count':1,'display':{'width':240,'height':280}}))
   board=json.loads((device/'board.json').read_text());board.update(id='community-c6-lcd',composition='../../compositions/c6-lcd-fixture',
    drivers=[],pins={'BUTTON':7,'LCD_CLK':18,'LCD_DATA':19,'TOUCH_IRQ':5},buttons=['BUTTON'],hardware={},display={'width':240,'height':280},
    runtime='../../runtime/headless-suite');(device/'board.json').write_text(json.dumps(board))
   with patch.object(boardpack,'HARDWARE',hardware),patch.object(catalog,'HARDWARE',hardware),patch.object(catalog,'ROOT',root):
    entries=catalog.catalog();self.assertEqual(entries['community-c6-lcd']['board']['mcu'],entries['e6-nova-c6']['board']['mcu'])
    self.assertNotIn('e6',entries['community-c6-lcd']['drivers'])
    self.assertEqual(catalog.select(['hardware/agent-os/compositions/c6-lcd-fixture/Port.inc']),['community-c6-lcd'])
    self.assertEqual(catalog.select(['hardware/agent-os/platforms/esp32-c6/platform.json']),['community-c6-lcd','e6-nova-c6'])
    self.assertEqual(catalog.select(['hardware/agent-os/drivers/e6/E6Driver.h']),['e6-nova-c6'])

 def test_staging_discards_unselected_drivers_and_previous_default_ui(self):
  with tempfile.TemporaryDirectory() as temp:
   stage=Path(temp)/'WishOS'
   builder.stage_sources(boardpack.DEFAULT,stage)
   self.assertTrue((stage/'src/drivers/esp-wakenet/EspWakeNet.h').exists())
   self.assertTrue((stage/'src/drivers/esp32-c6-radio/RadioMux.h').exists())
   self.assertTrue((stage/'src/drivers/e6/scene/Content.cpp').exists())
   self.assertTrue((stage/'src/display/NativeText.cpp').exists())
   self.assertFalse((stage/'src/display/Codec.h').exists())
   self.assertNotIn('/ui/welcome.json',(stage/'src/BoardPackage.h').read_text())
   self.assertIn('/recovery/ui.json',(stage/'src/BoardPackage.h').read_text())
   builder.stage_sources(boardpack.HARDWARE/'boards/esp32-c3-runtime',stage)
   self.assertEqual([p.name for p in (stage/'src/drivers').iterdir()],['esp32-services'])
   self.assertNotIn('/ui/welcome.json',(stage/'src/BoardPackage.h').read_text())
   self.assertFalse((stage/'src/Content.cpp').exists())
   self.assertFalse((stage/'src/drivers/e6').exists())
   before=(stage/'src/BoardPackage.h').read_bytes()
   broken=Path(temp)/'broken';broken.mkdir();(broken/'board.json').write_text('{}')
   with self.assertRaises(ValueError):builder.stage_sources(broken,stage)
   self.assertEqual(before,(stage/'src/BoardPackage.h').read_bytes())

 def test_optional_drivers_are_compiled_only_when_selected(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);board=json.loads((boardpack.DEFAULT/'board.json').read_text())
   for key in ('composition','runtime'):board[key]=str((boardpack.DEFAULT/board[key]).resolve())
   shutil.copy2(boardpack.DEFAULT/'partitions.csv',root/'partitions.csv')
   board['drivers']=['esp32-c6-radio','esp-wakenet'];(root/'board.json').write_text(json.dumps(board))
   spec,port=builder.stage_sources(root,root/'WishOS')
   selected=boardpack.driver_packages(spec,port)
   self.assertIn('esp32-c6-radio',selected);self.assertIn('esp-wakenet',selected)
   self.assertIn('-DWISHOS_C6_MESH=1',selected['esp32-c6-radio'][1]['flags'])
   self.assertTrue((root/'WishOS/src/drivers/esp-wakenet/EspWakeNet.h').is_file())
   self.assertTrue((root/'WishOS/src/drivers/esp32-c6-radio/RadioMux.h').is_file())

 def test_driver_cycle_and_incompatible_target_are_rejected(self):
  with tempfile.TemporaryDirectory() as temp:
   hardware=Path(temp);drivers=hardware/'drivers';drivers.mkdir()
   for name,requires in [('a',['b']),('b',['a'])]:
    p=drivers/name;p.mkdir();(p/'driver.json').write_text(json.dumps({'format':'notes-driver/1','id':name,'requires':requires}))
   with patch.object(boardpack,'HARDWARE',hardware):
    with self.assertRaisesRegex(ValueError,'cycle'):boardpack.driver_packages({}, {'drivers':['a']})
    (drivers/'a/driver.json').write_text(json.dumps({'format':'notes-driver/1','id':'a','targets':['other']}))
    with self.assertRaisesRegex(ValueError,'target'):boardpack.driver_packages({}, {'drivers':['a'],'target':'test'})

 def test_firmware_archive_checks_hash_and_contains_only_selected_build(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);names=['WishOS.ino.bin','WishOS.ino.bootloader.bin','WishOS.ino.partitions.bin','boot_app0.bin','partitions.csv','flash_args']
   files={}
   for name in names:(root/name).write_bytes(name.encode());files[name]=hashlib.sha256(name.encode()).hexdigest()
   (root/'unrelated-secret.txt').write_text('must not be packed')
   (root/'firmware-manifest.json').write_text(json.dumps({'format':'notes-firmware-build/1','board':'unit','mcu':'test','revision':'abc','files':files}))
   output=release.package(root,root/'firmware.zip')
   with zipfile.ZipFile(output) as archive:
    self.assertFalse(any('secret' in n for n in archive.namelist()))
    manifest=json.loads(archive.read('unit/release-manifest.json'))
    self.assertEqual(set(manifest['files']),{n[5:] for n in archive.namelist()}-{'release-manifest.json'})
    for name,digest in manifest['files'].items():self.assertEqual(hashlib.sha256(archive.read('unit/'+name)).hexdigest(),digest)
   (root/names[0]).write_bytes(b'changed')
   with self.assertRaisesRegex(ValueError,'differs'):release.package(root,output)

 def test_speech_snapshot_selects_target_and_rejects_changed_generated_files(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);manifest=root/'speech.json';raw=root/'build/dependencies/esp-sr-pinned'
   payloads={'LICENSE':b'license','include/esp32c3/engine.h':b'header','lib/esp32c3/engine.a':b'archive','src/include/model_path.h':b'model','src/include/esp_sr_debug.h':b'debug','src/esp_sr_debug.c':b'debug',
    'model/wakenet_model/wn9s_hiesp/_MODEL_INFO_':b'info','model/wakenet_model/wn9s_hiesp/wn9_data':b'model-data','model/wakenet_model/wn9s_hiesp/wn9_index':b'index'}
   for name,data in payloads.items():
    file=raw/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(data)
   (raw/'include/esp32c3/unpinned.h').write_text('must not compile');(raw/'lib/esp32c3/unpinned.a').write_text('must not link')
   manifest.write_text(json.dumps({'commit':'pinned','targets':{'esp32c3':{'model':'wn9s_hiesp'}},'files':[{'path':name,'sha256':hashlib.sha256(data).hexdigest()}for name,data in payloads.items()]}))
   fft=root/'tools/dependencies/dl-fft.json';fft.parent.mkdir(parents=True);fft.write_text(json.dumps({'commit':'fft','files':[]}))
   with patch.object(speech,'ROOT',root),patch.object(speech,'MANIFEST',manifest):
    library=speech.prepare('esp32c3');self.assertEqual(speech.prepare('esp32c3'),library);self.assertFalse((library/'src/unpinned.h').exists());self.assertFalse((library/'src/esp32c3/unpinned.a').exists())
    (library/'src/engine.h').write_text('uncommitted edit')
    with self.assertRaisesRegex(ValueError,'snapshot modified'):speech.prepare('esp32c3')
    with self.assertRaisesRegex(ValueError,'not supported'):speech.prepare('esp32s3')
 def test_factory_filesystem_is_required_and_hash_verified_in_release(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);names=['WishOS.ino.bin','WishOS.ino.bootloader.bin','WishOS.ino.partitions.bin','boot_app0.bin','partitions.csv','flash_args']
   files={}
   for name in names:(root/name).write_bytes(b'fixture');files[name]=hashlib.sha256(b'fixture').hexdigest()
   manifest={'format':'notes-firmware-build/1','board':'unit','mcu':'test','revision':'abc','files':files,'filesystem':{'image':'runtime.littlefs.bin'}}
   meta=root/'firmware-manifest.json';meta.write_text(json.dumps(manifest))
   with self.assertRaisesRegex(ValueError,'filesystem image missing'):release.package(root,root/'output.zip')
   (root/'runtime.littlefs.bin').write_bytes(b'filesystem');files['runtime.littlefs.bin']=hashlib.sha256(b'filesystem').hexdigest();meta.write_text(json.dumps(manifest));release.package(root,root/'output.zip')
   with zipfile.ZipFile(root/'output.zip')as archive:self.assertIn('unit/runtime.littlefs.bin',archive.namelist());self.assertIn(b'INITIAL',archive.read('unit/README.txt'))
   (root/'runtime.littlefs.bin').write_bytes(b'corrupt')
   with self.assertRaisesRegex(ValueError,'differs'):release.package(root,root/'output.zip')
if __name__=='__main__':unittest.main()
