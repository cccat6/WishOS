#!/usr/bin/env python3
"""Compose MCU platform, device wiring/native composition and runtime assets."""
import argparse
import hashlib
import json
import math
import re
import shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'hardware/agent-os/boards/e6-nova-c6'
HARDWARE = ROOT / 'hardware/agent-os'

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))

def inside(root, relative):
    p = (root / relative).resolve()
    if not p.is_relative_to(root.resolve()):
        raise ValueError('package file escapes its directory')
    return p

def driver_packages(board, port):
    """Resolve the native dependency closure, once, in dependency-first order."""
    optional = board.get('drivers', [])
    if not isinstance(optional,list) or len(set(optional)) != len(optional) or any(d not in port.get('optional_drivers',[]) for d in optional):
        raise ValueError('unsupported optional driver')
    result, visiting = {}, set()
    def visit(name):
        if not isinstance(name,str) or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,63}',name):
            raise ValueError('invalid driver id')
        if name in result:return
        if name in visiting:raise ValueError('driver dependency cycle: '+name)
        directory=HARDWARE/'drivers'/name
        spec=read_json(directory/'driver.json')
        if spec.get('format')!='notes-driver/1' or spec.get('id')!=name:raise ValueError('invalid driver manifest: '+name)
        if spec.get('platforms') and port.get('platform') not in spec['platforms']:raise ValueError('driver/platform mismatch: '+name)
        if spec.get('targets') and port.get('target') not in spec['targets']:raise ValueError('driver/target mismatch: '+name)
        visiting.add(name)
        for dependency in spec.get('requires',[]):visit(dependency)
        visiting.remove(name)
        if spec.get('build_hook') and not inside(directory,spec['build_hook']).is_file():raise ValueError('driver build hook missing: '+name)
        result[name]=(directory,spec)
    for name in port.get('drivers',[])+optional:visit(name)
    return result

def runtime_package(root, board):
    if not board.get('runtime'):return root,{}
    directory=(root/board['runtime']).resolve()
    package=read_json(directory/'package.json')
    if package.get('format')!='notes-runtime-package/1':raise ValueError('invalid runtime package')
    return directory,package

def platform_package(board):
    name=board['platform']
    if not isinstance(name,str) or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,63}',name):raise ValueError('invalid platform id')
    directory=HARDWARE/'platforms'/name
    spec=read_json(directory/'platform.json')
    if spec.get('format')!='notes-platform/1' or spec.get('id')!=name:raise ValueError('invalid MCU platform')
    if board.get('mcu') not in spec['mcu_variants']:raise ValueError('platform/MCU mismatch')
    return directory,spec

def validate_hardware(values, schema):
    if set(values)!=set(schema):raise ValueError('hardware fields do not match hardware composition')
    for name,value in values.items():
        if not re.fullmatch(r'[a-z][a-z0-9_]*',name):raise ValueError('invalid hardware field name')
        spec=schema[name];kind=spec['type']
        types={'boolean':bool,'integer':int,'string':str,'array':list,'object':dict}
        valid=(type(value) in (int,float) and math.isfinite(value)) if kind=='number' else kind in types and type(value) is types[kind]
        if not valid:raise ValueError('hardware type: '+name)
        if kind=='integer' and not -(2**63)<=value<2**63:raise ValueError('hardware integer representation: '+name)
        if 'enum' in spec and value not in spec['enum']:raise ValueError('hardware choice: '+name)
        if kind in ('integer','number'):
            if 'minimum' in spec and value<spec['minimum']:raise ValueError('hardware minimum: '+name)
            if 'maximum' in spec and value>spec['maximum']:raise ValueError('hardware maximum: '+name)
            if 'exclusiveMinimum' in spec and value<=spec['exclusiveMinimum']:raise ValueError('hardware minimum: '+name)
        if kind=='string' and 'maxLength' in spec and len(value)>spec['maxLength']:raise ValueError('hardware string length: '+name)
    json.dumps(values,allow_nan=False)

def asset_sources(root, board):
    directory,package=runtime_package(root,board)
    sources={k:(directory,v) for k,v in package.get('assets',{}).items()}
    sources.update({k:(root,v) for k,v in board.get('assets',{}).items()})
    return {k:inside(HARDWARE/'runtime',v[9:]) if isinstance(v,str) and v.startswith('@runtime/') else inside(base,v) for k,(base,v) in sources.items()}

def load(package):
    root = Path(package).resolve()
    b = read_json(root / 'board.json')
    if b.get('format') != 'notes-board/1' or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,63}', b.get('id','')):
        raise ValueError('invalid board format/id')
    _,platform=platform_package(b)
    port = (root / b['composition']).resolve()
    composition = read_json(port / 'composition.json')
    if composition.get('format')!='notes-composition/1' or b['platform'] not in composition['platforms']:
        raise ValueError('composition/platform mismatch')
    # MCU capabilities do not define peripherals. A concrete device combines the
    # MCU with its own native composition, signal names, partition layout and UI.
    p={**composition,'platform_id':platform['id'],'platform':platform['family'],
       'target':platform['target'],'toolchain':platform['toolchain'],
       'available_gpios':platform['available_gpios'],'package_pins':platform.get('package_pins',{}),
       'drivers':platform.get('drivers',[])+composition.get('drivers',[])}
    p.update({k:b[k] for k in ('fqbn','application_bytes','partition_file')})
    if not inside(root,b['partition_file']).is_file():raise ValueError('device partition table missing')
    if not inside(port, p['entry']).is_file():
        raise ValueError('native composition entry missing')
    if b.get('display') != p.get('display'):
        raise ValueError('display geometry not supported by selected composition')
    for key in ['asset_file_limit_bytes','asset_budget_bytes']:
        if key in p and (type(p[key]) is not int or p[key]<1):raise ValueError('invalid port asset budget')
    driver_packages(b,p)
    pins = b['pins']
    if set(pins) != set(p['required_pins']):
        raise ValueError('pin set does not match selected hardware composition')
    if any(not re.fullmatch(r'[A-Z][A-Z0-9_]*', k) for k in pins):
        raise ValueError('invalid pin signal')
    if any(type(v) is not int or v not in p['available_gpios'] for v in pins.values()):
        raise ValueError('unavailable GPIO')
    for gpio in set(pins.values()):
        signals={name for name,value in pins.items() if value==gpio}
        if len(signals)>1 and not any(signals<=set(group) for group in p.get('shared_pin_groups',[])):
            raise ValueError('conflicting GPIO assignments; declare shared signals in the native composition')
    for signal, allowed in p.get('pin_constraints', {}).items():
        if pins[signal] not in allowed:
            raise ValueError(f'{signal} incompatible with required hardware capability')
    h = b.get('hardware',{})
    validate_hardware(h,p.get('hardware_schema',{}))
    buttons=b.get('buttons',[])
    if len(buttons)!=p.get('button_count',len(buttons)) or len(set(buttons))!=len(buttons) or any(k not in pins for k in buttons):
        raise ValueError('invalid button mapping')
    assets = {}
    for vpath, asset in asset_sources(root,b).items():
        if len(vpath)>=256 or not vpath.startswith('/') or str(Path(vpath)) != vpath or '..' in Path(vpath).parts or '\\' in vpath:
            raise ValueError('invalid asset path')
        raw = asset.read_bytes()
        if p.get('asset_file_limit_bytes') and len(raw)>p['asset_file_limit_bytes']:
            raise ValueError('system asset limit')
        if Path(vpath).suffix in ('.json','.lua','.html','.js','.css','.svg','.txt'):
            text=raw.decode('utf-8')
            if '\0' in text:raise ValueError('NUL in text asset')
            if vpath.endswith('.json'):text=json.dumps(json.loads(text),ensure_ascii=False,separators=(',',':'))
            assets[vpath]=text
        else:assets[vpath]=raw
    if p.get('asset_budget_bytes') and sum(len(x.encode()) if isinstance(x,str) else len(x) for x in assets.values())>p['asset_budget_bytes']:
        raise ValueError('compiled asset budget')
    defaults = json.loads(assets.get('/defaults.json','{}'))
    if not isinstance(defaults,dict):
        raise ValueError('defaults object required')
    memory=b.get('resources',{})
    if not isinstance(memory,dict) or set(memory)-{'heap_reserve_bytes','external_heap_reserve_bytes','default_vm_memory_bytes','application_memory','memory_limit_bytes'}:
        raise ValueError('invalid resource defaults')
    for key,value in memory.items():
        if key=='application_memory':
            if value not in ('auto','internal','external'):raise ValueError('invalid memory region')
        elif key=='memory_limit_bytes' and value is None:pass
        elif type(value) is not int or value<0 or value>0xffffffff or (key=='default_vm_memory_bytes' and value<16384) or (key=='memory_limit_bytes' and value==0):
            raise ValueError('invalid resource default: '+key)
    _,runtime=runtime_package(root,b)
    b['startup']=b.get('startup',runtime.get('startup',{}))
    if not isinstance(b['startup'], dict) or set(b['startup']) - {'script','memory_bytes'}:
        raise ValueError('startup must reference an optional runtime script')
    memory_hint=b['startup'].get('memory_bytes')
    if memory_hint is not None and (type(memory_hint) is not int or not 16384<=memory_hint<=0xffffffff):
        raise ValueError('invalid startup memory ceiling')
    script=b['startup'].get('script','')
    if not isinstance(script,str) or (script and (not script.startswith('/system/') or not isinstance(assets.get(script[7:]),str) or len(assets[script[7:]].encode())>8192)):
        raise ValueError('startup script missing or too large')
    return root,b,port,p,assets

def literal(value):
    return json.dumps(value,ensure_ascii=False)

def generate(package, output):
    root,b,port,p,assets=load(package)
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    port_hashes={str(f.relative_to(port)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(port.rglob('*')) if f.is_file()}
    asset_hashes={k:hashlib.sha256(v.encode() if isinstance(v,str) else v).hexdigest() for k,v in assets.items()}
    drivers=driver_packages(b,p)
    driver_hashes={name:{str(f.relative_to(directory)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(directory.rglob('*')) if f.is_file() and '__pycache__' not in f.parts} for name,(directory,_) in drivers.items()}
    _,platform=platform_package(b)
    partition_hash=hashlib.sha256(inside(root,b['partition_file']).read_bytes()).hexdigest()
    fingerprint=hashlib.sha256(json.dumps({'board':b,'composition':p,'platform':platform,'partitions':partition_hash,'assets':asset_hashes,'composition_files':port_hashes,'drivers':driver_hashes},sort_keys=True).encode()).hexdigest()
    lines=['#pragma once','#include <stddef.h>','#include <stdint.h>','namespace notesos::boardpack {',f'inline constexpr char ID[]={literal(b["id"])};',f'inline constexpr char MCU[]={literal(b["mcu"])};',f'inline constexpr char FINGERPRINT[]={literal(fingerprint)};',f'inline constexpr char STARTUP_SCRIPT[]={literal(b["startup"].get("script",""))};','struct Asset {const char*path;const char*data;size_t size;};','inline constexpr Asset ASSETS[]={']
    insertion=lines.index('struct Asset {const char*path;const char*data;size_t size;};')
    lines.insert(insertion,f'inline constexpr size_t STARTUP_MEMORY_BYTES={b["startup"].get("memory_bytes") or 0};')
    _,runtime=runtime_package(root,b)
    lines.insert(insertion,f'inline constexpr char RUNTIME_ID[]={literal(runtime.get("id","board-local"))};')
    lines.insert(insertion,f'inline constexpr char DRIVERS[]={literal(",".join(drivers))};')
    lines.insert(insertion,f'inline constexpr char PLATFORM_ID[]={literal(b["platform"])};')
    lines.insert(insertion,f'inline constexpr char COMPOSITION_ID[]={literal(p["id"])};')
    for key,value in sorted(assets.items()):
        raw=value.encode() if isinstance(value,str) else value
        encoded=literal(value) if isinstance(value,str) else '"'+''.join(f'\\x{x:02x}' for x in raw)+'"'
        lines.append(f'{{{literal(key)},{encoded},{len(raw)}}},')
    lines += ['};','inline constexpr size_t ASSET_COUNT=sizeof(ASSETS)/sizeof(ASSETS[0]);','struct Pin {const char*signal;int gpio;int pad;};','inline constexpr Pin PINS[]={']
    lines += [f'{{{literal(k)},{v},{p.get("package_pins",{}).get(str(v),-1)}}},' for k,v in b['pins'].items()]
    lines += ['};',f'inline constexpr char RESOURCE_DEFAULTS[]={literal(json.dumps(b.get("resources",{}),separators=(",",":")))};',f'inline constexpr char HARDWARE_JSON[]={literal(json.dumps(b.get("hardware",{}),separators=(",",":")))};',f'inline constexpr int DISPLAY_WIDTH={(b.get("display") or {}).get("width",0)};',f'inline constexpr int DISPLAY_HEIGHT={(b.get("display") or {}).get("height",0)};','}','namespace notesos::pins {']
    lines += [f'inline constexpr int {k}={v};' for k,v in b['pins'].items()]
    if b.get('buttons'):
        lines += ['inline constexpr int buttons[]={'+','.join(b['buttons'])+'};']
    for k,v in b.get('hardware',{}).items():
        if type(v) is bool:
            lines += [f'#define WISHOS_HW_{k.upper()} {int(v)}',f'inline constexpr bool {k.upper()}={str(v).lower()};']
        elif type(v) in (int,float):
            kind='int64_t' if type(v) is int else 'double'
            value='(-9223372036854775807LL-1)' if v==-(2**63) else str(v)
            lines += [f'inline constexpr {kind} {k.upper()}={value};']
        elif type(v) is str:lines += [f'inline constexpr char {k.upper()}[]={literal(v)};']
    lines += ['}']
    (output/'BoardPackage.h').write_text('\n'.join(lines)+'\n')
    return b,p,port

def stage(package, sketch):
    sketch=Path(sketch);b,p,port=generate(package,sketch/'src')
    selected=sketch/'src/selected'
    if selected.exists():shutil.rmtree(selected)
    shutil.copytree(port,selected)
    driver_root=sketch/'src/drivers'
    if driver_root.exists():shutil.rmtree(driver_root)
    driver_root.mkdir()
    for name,(directory,_) in driver_packages(b,p).items():
        shutil.copytree(directory,driver_root/name,ignore=shutil.ignore_patterns('__pycache__','*.py','driver.json','*.md'))
    (sketch/'src/Hardware.cpp').write_text('#include "Hardware.h"\n#include "selected/'+p['entry']+'"\n')
    shutil.copy2(inside(Path(package),b['partition_file']),sketch/'partitions.csv')
    (sketch/'board-manifest.json').write_text(json.dumps(b,indent=2)+'\n')
    return b,p

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('package',nargs='?',default=str(DEFAULT));parser.add_argument('--output');a=parser.parse_args()
    if a.output:generate(a.package,a.output)
    else:load(a.package)
    print('Board package valid')
