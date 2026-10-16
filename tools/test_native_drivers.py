#!/usr/bin/env python3
"""Compile optional adapters against an ESP32 SDK; no device access or flashing."""
import argparse,shutil,subprocess
from pathlib import Path
from prepare_arduino import prepare_library,cli_path,cli_config,verify_cli
from prepare_board_package import ROOT
from prepare_speech import prepare as prepare_speech

def main():
 p=argparse.ArgumentParser();p.add_argument('--target',default='esp32s3',choices=['esp32c3','esp32c6','esp32s3']);a=p.parse_args();verify_cli()
 stage=ROOT/'build/native-drivers'/a.target/'NativeDrivers';stage.mkdir(parents=True,exist_ok=True)
 if (stage/'src').exists():shutil.rmtree(stage/'src')
 (stage/'src').mkdir()
 for source in (ROOT/'firmware/WishOS/src').glob('*.h'):shutil.copy2(source,stage/'src'/source.name)
 shutil.copytree(ROOT/'firmware/WishOS/src/lua',stage/'src/lua')
 shutil.copytree(ROOT/'firmware/WishOS/src/audio',stage/'src/audio')
 for name in ['i2c','sensor-sampling','gxhtc3','qmc5883l','qmi8658','ft6336','pca9557','esp-wire-i2c','esp-wakenet']:
  shutil.copytree(ROOT/'hardware/agent-os/drivers'/name,stage/'src/drivers'/name)
 shutil.copy2(ROOT/'tests/native-drivers/NativeDrivers.ino',stage/'NativeDrivers.ino')
 (stage/'build_opt.h').write_text('-I"'+str(stage/'src')+'"\n')
 subprocess.run([cli_path(),'compile',*cli_config(),'--fqbn','esp32:esp32:'+a.target,'--library',str(prepare_library('ArduinoJson')),'--library',str(prepare_speech(a.target)),'--build-property','tools.ctags.pattern=true','--warnings','none',str(stage)],check=True)

if __name__=='__main__':main()
