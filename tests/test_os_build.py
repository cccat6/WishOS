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
import agentic_os_catalog as catalog
import prepare_board_package as boardpack
import build_agentic_os as builder
import release_agentic_os as release

class BuildTests(unittest.TestCase):
 def test_change_selection_follows_driver_and_runtime_ownership(self):
  self.assertEqual(catalog.select(['hardware/agent-os/boards/esp32-c3-runtime/board.json']),['esp32-c3-runtime'])
  self.assertEqual(catalog.select(['hardware/agent-os/drivers/e6/E6Driver.h']),['e6-nova-c6'])
  self.assertEqual(catalog.select(['hardware/agent-os/runtime/headless-suite/assets/setup.html']),['esp32-c3-runtime'])
  self.assertEqual(catalog.select(['hardware/agent-os/runtime/lib/input.lua']),['e6-nova-c6','esp32-c3-runtime'])
  self.assertEqual(catalog.select(['firmware/AgenticOS/src/Core.h']),['e6-nova-c6','esp32-c3-runtime'])
  self.assertEqual(catalog.select(['docs/agent-os-plan.md']),[])
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
   self.assertEqual(catalog.select(['tools/dependencies/arduino.json'],previous_lock=previous),['e6-nova-c6'])

 def test_new_board_is_discovered_without_editing_matrix_or_kernel(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);hardware=root/'hardware/agent-os';shutil.copytree(boardpack.HARDWARE,hardware)
   new=hardware/'boards/community-c3';shutil.copytree(hardware/'boards/esp32-c3-runtime',new)
   spec=json.loads((new/'board.json').read_text());spec['id']='community-c3';spec['name']='Community wiring variant';(new/'board.json').write_text(json.dumps(spec))
   with patch.object(boardpack,'HARDWARE',hardware),patch.object(catalog,'HARDWARE',hardware),patch.object(catalog,'ROOT',root):
    self.assertEqual(catalog.select(['hardware/agent-os/boards/community-c3/board.json']),['community-c3'])
    self.assertEqual(catalog.select(['hardware/agent-os/runtime/headless-suite/assets/setup.html']),['community-c3','esp32-c3-runtime'])

 def test_staging_discards_unselected_drivers_and_previous_default_ui(self):
  with tempfile.TemporaryDirectory() as temp:
   stage=Path(temp)/'AgenticOS'
   builder.stage_sources(boardpack.DEFAULT,stage)
   self.assertTrue((stage/'src/drivers/esp-wakenet/EspWakeNet.h').exists())
   self.assertIn('欢迎',(stage/'src/BoardPackage.h').read_text())
   builder.stage_sources(boardpack.HARDWARE/'boards/esp32-c3-runtime',stage)
   self.assertEqual([p.name for p in (stage/'src/drivers').iterdir()],['esp32-services'])
   self.assertNotIn('/ui/welcome.json',(stage/'src/BoardPackage.h').read_text())
   self.assertFalse((stage/'src/Content.cpp').exists())
   before=(stage/'src/BoardPackage.h').read_bytes()
   broken=Path(temp)/'broken';broken.mkdir();(broken/'board.json').write_text('{}')
   with self.assertRaises(ValueError):builder.stage_sources(broken,stage)
   self.assertEqual(before,(stage/'src/BoardPackage.h').read_bytes())

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
   root=Path(temp);names=['AgenticOS.ino.bin','AgenticOS.ino.bootloader.bin','AgenticOS.ino.partitions.bin','boot_app0.bin','partitions.csv','flash_args']
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

if __name__=='__main__':unittest.main()
