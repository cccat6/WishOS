#!/usr/bin/env python3
"""Validate and materialize a board/port package without editing the OS source."""
import argparse
import hashlib
import json
import re
import shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'hardware/agent-os/boards/e6-nova-c6'

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))

def inside(root, relative):
    p = (root / relative).resolve()
    if not p.is_relative_to(root.resolve()):
        raise ValueError('package file escapes its directory')
    return p

def load(package):
    root = Path(package).resolve()
    b = read_json(root / 'board.json')
    if b.get('format') != 'notes-board/1' or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,63}', b.get('id','')):
        raise ValueError('invalid board format/id')
    port = (root / b['port']).resolve()
    p = read_json(port / 'port.json')
    if p.get('format') != 'notes-port/1' or b.get('mcu') != p.get('mcu'):
        raise ValueError('port/MCU mismatch')
    if not inside(port, p['entry']).is_file():
        raise ValueError('port entry missing')
    if b.get('display') != p.get('display'):
        raise ValueError('display geometry not supported by selected port')
    pins = b['pins']
    if set(pins) != set(p['required_pins']):
        raise ValueError('pin set does not match selected port')
    if any(not re.fullmatch(r'[A-Z][A-Z0-9_]*', k) for k in pins):
        raise ValueError('invalid pin signal')
    if any(type(v) is not int or v not in p['available_gpios'] for v in pins.values()):
        raise ValueError('unavailable GPIO')
    if len(set(pins.values())) != len(pins):
        raise ValueError('conflicting GPIO assignments; shared buses use one named bus')
    for signal, allowed in p.get('pin_constraints', {}).items():
        if pins[signal] not in allowed:
            raise ValueError(f'{signal} incompatible with required hardware capability')
    h = b.get('hardware',{})
    if set(h) != set(p.get('hardware_fields',[])):
        raise ValueError('hardware fields do not match port')
    for k,v in h.items():
        if not re.fullmatch(r'[a-z][a-z0-9_]*',k) or type(v) not in (bool,int,float) or (type(v) is not bool and not 0 < v < 1_000_001):
            raise ValueError('invalid hardware scalar: '+k)
    buttons=b.get('buttons',[])
    if len(buttons)!=p.get('button_count',len(buttons)) or len(set(buttons))!=len(buttons) or any(k not in pins for k in buttons):
        raise ValueError('invalid button mapping')
    assets = {}
    for vpath, local in b['assets'].items():
        if len(vpath)>=256 or not vpath.startswith('/') or str(Path(vpath)) != vpath or '..' in Path(vpath).parts or '\\' in vpath:
            raise ValueError('invalid asset path')
        raw = inside(root,local).read_bytes()
        if len(raw)>16384 or b'\0' in raw:
            raise ValueError('system text asset limit')
        text = raw.decode('utf-8')
        if vpath.endswith('.json'):
            text = json.dumps(json.loads(text),ensure_ascii=False,separators=(',',':'))
        assets[vpath] = text
    if sum(len(x.encode()) for x in assets.values())>65536:
        raise ValueError('compiled asset budget')
    defaults = json.loads(assets.get('/defaults.json','{}'))
    if not isinstance(defaults,dict):
        raise ValueError('defaults object required')
    ui = b['startup']['ui']
    if not ui.startswith('/system/') or ui[7:] not in assets:
        raise ValueError('startup UI missing')
    script=b['startup'].get('script','')
    if script and (not script.startswith('/system/') or script[7:] not in assets or len(assets[script[7:]].encode())>8192):
        raise ValueError('startup script missing or too large')
    return root,b,port,p,assets

def literal(value):
    return json.dumps(value,ensure_ascii=False)

def generate(package, output):
    root,b,port,p,assets=load(package)
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    port_hashes={str(f.relative_to(port)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(port.rglob('*')) if f.is_file()}
    fingerprint=hashlib.sha256(json.dumps({'board':b,'port':p,'assets':assets,'port_files':port_hashes},sort_keys=True).encode()).hexdigest()
    lines=['#pragma once','#include <stddef.h>','#include <stdint.h>',f'#define NOTES_HAS_VBUS_ADC {int(b.get("hardware",{}).get("vbus_clamps_battery_adc",False))}','namespace notesos::boardpack {',f'inline constexpr char ID[]={literal(b["id"])};',f'inline constexpr char MCU[]={literal(b["mcu"])};',f'inline constexpr char FINGERPRINT[]={literal(fingerprint)};',f'inline constexpr char STARTUP_UI[]={literal(b["startup"]["ui"])};',f'inline constexpr char STARTUP_SCRIPT[]={literal(b["startup"].get("script",""))};','struct Asset {const char*path;const char*data;};','inline constexpr Asset ASSETS[]={']
    lines += [f'{{{literal(k)},{literal(v)}}},' for k,v in sorted(assets.items())]
    lines += ['};','inline constexpr size_t ASSET_COUNT=sizeof(ASSETS)/sizeof(ASSETS[0]);','struct Pin {const char*signal;int gpio;int pad;};','inline constexpr Pin PINS[]={']
    lines += [f'{{{literal(k)},{v},{p.get("package_pins",{}).get(str(v),-1)}}},' for k,v in b['pins'].items()]
    lines += ['};',f'inline constexpr int DISPLAY_WIDTH={(b.get("display") or {}).get("width",0)};',f'inline constexpr int DISPLAY_HEIGHT={(b.get("display") or {}).get("height",0)};',f'inline constexpr int CRYSTAL_MHZ={b.get("hardware",{}).get("crystal_mhz",0)};',f'inline constexpr float CRYSTAL_LOAD_PF={b.get("hardware",{}).get("crystal_load_pf",0)};','}','namespace notesos::pins {']
    lines += [f'inline constexpr int {k}={v};' for k,v in b['pins'].items()]
    if b.get('buttons'):
        lines += ['inline constexpr int buttons[]={'+','.join(b['buttons'])+'};']
    for k,v in b.get('hardware',{}).items():
        kind='bool' if type(v) is bool else 'float' if type(v) is float else 'uint32_t'
        value=str(v).lower()
        lines += [f'inline constexpr {kind} {k.upper()}={value};']
    lines += ['}']
    (output/'BoardPackage.h').write_text('\n'.join(lines)+'\n')
    return b,p,port

def stage(package, sketch):
    sketch=Path(sketch);b,p,port=generate(package,sketch/'src')
    selected=sketch/'src/selected'
    if selected.exists():shutil.rmtree(selected)
    shutil.copytree(port,selected)
    (sketch/'src/Hardware.cpp').write_text('#include "Hardware.h"\n#include "selected/'+p['entry']+'"\n')
    shutil.copy2(inside(port,p['partition_file']),sketch/'partitions.csv')
    (sketch/'board-manifest.json').write_text(json.dumps(b,indent=2)+'\n')
    return b,p

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('package',nargs='?',default=str(DEFAULT));parser.add_argument('--output');a=parser.parse_args()
    if a.output:generate(a.package,a.output)
    else:load(a.package)
    print('Board package valid')
