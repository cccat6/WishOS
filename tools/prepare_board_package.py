#!/usr/bin/env python3
"""Compose MCU platform, device wiring/native composition and runtime assets."""
import argparse
import gzip
import hashlib
import json
import math
import re
import shutil
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
DEFAULT = ROOT / 'hardware/agent-os/boards/lckfb-szpi-esp32s3'
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

def compiled_assets(root,board,assets):
    directory,runtime=runtime_package(root,board)
    standard={'/lib/'+file.name:file.read_text() for file in (ROOT/'firmware/WishOS/lib').glob('*.lua')}
    if 'compiled_assets' not in runtime:return {**standard,**assets}
    if not board.get('runtime_filesystem'):raise ValueError('compiled recovery requires filesystem runtime')
    selected={}
    for key,relative in runtime['compiled_assets'].items():
        if not isinstance(key,str) or not key.startswith('/') or '..' in key.split('/'):raise ValueError('compiled recovery path')
        raw=inside(directory,relative).read_bytes()
        selected[key]=assets[key] if key in ('/applications.json','/defaults.json','/services.json') and key in assets else raw.decode() if Path(relative).suffix in ('.json','.lua','.html') else raw
    selected['/components.json']=assets['/components.json']
    return {**standard,**selected}

def load(package):
    root = Path(package).resolve()
    b = read_json(root / 'board.json')
    flash=b.get('flash_bytes')
    if type(flash) is not int or flash<4194304 or flash&(flash-1):raise ValueError('WishOS requires declared Flash capacity of at least 4 MiB')
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
       'drivers':platform.get('drivers',[])+composition.get('drivers',[]),
       'flags':list(dict.fromkeys(platform.get('flags',[])+composition.get('flags',[])))}
    p.update({k:b[k] for k in ('fqbn','application_bytes','partition_file')})
    if not inside(root,b['partition_file']).is_file():raise ValueError('device partition table missing')
    from flash_layout import validate as validate_flash_layout
    validate_flash_layout(root,b)
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
    _,runtime=runtime_package(root,b)
    compressed=b.get('gzip_assets',runtime.get('gzip_assets',[]))
    sources=asset_sources(root,b)
    if not isinstance(compressed,list) or any(not isinstance(path,str) or path not in sources or Path(path).suffix not in ('.html','.js','.css') for path in compressed):raise ValueError('invalid compressed web asset')
    for vpath, asset in sources.items():
        if len(vpath)>=256 or not vpath.startswith('/') or str(Path(vpath)) != vpath or '..' in Path(vpath).parts or '\\' in vpath:
            raise ValueError('invalid asset path')
        raw = asset.read_bytes()
        if Path(vpath).suffix=='.lua' and (not raw or len(raw)>8192):raise ValueError('runtime Lua asset empty or too large: '+vpath)
        if p.get('asset_file_limit_bytes') and len(raw)>p['asset_file_limit_bytes']:
            raise ValueError('system asset limit')
        if Path(vpath).suffix in ('.json','.lua','.html','.js','.css','.svg','.txt'):
            text=raw.decode('utf-8')
            if '\0' in text:raise ValueError('NUL in text asset')
            if vpath.endswith('.json'):text=json.dumps(json.loads(text),ensure_ascii=False,separators=(',',':'))
            assets[vpath]=text
        else:assets[vpath]=raw
        if vpath in compressed:
            value=assets.pop(vpath);encoded=value.encode() if isinstance(value,str) else value
            assets[vpath+'.gz']=gzip.compress(encoded,compresslevel=9,mtime=0)
    if '/components.json' in assets:raise ValueError('components.json is generated by the system packager')
    generated=json.dumps(component_index(root,b,p),ensure_ascii=False,separators=(',',':'))
    if p.get('asset_file_limit_bytes') and len(generated.encode())>p['asset_file_limit_bytes']:raise ValueError('system asset limit')
    assets['/components.json']=generated
    selected=compiled_assets(root,b,assets)
    if p.get('asset_budget_bytes') and sum(len(x.encode()) if isinstance(x,str) else len(x) for x in selected.values())>p['asset_budget_bytes']:
        raise ValueError('compiled asset budget')
    defaults = json.loads(assets.get('/defaults.json','{}'))
    if not isinstance(defaults,dict):
        raise ValueError('defaults object required')
    providers=b.get('application_providers',[])
    if not isinstance(providers,list) or len(set(providers))!=len(providers) or any(not isinstance(x,str) or not re.fullmatch(r'[a-z][a-z0-9_-]{0,47}',x) for x in providers):raise ValueError('invalid application providers')
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
    if b.get('startup') or runtime.get('startup'):
        raise ValueError('startup is defined by applications.json launcher reference')
    applications=json.loads(assets.get('/applications.json','{}'))
    if runtime and (not applications or not applications.get('launcher')):raise ValueError('runtime package requires a factory launcher')
    if applications:
        records=applications.get('applications',[])
        if not isinstance(records,list):raise ValueError('invalid application catalog')
        ids=set()
        for record in records:
            app_id=record.get('id');path=record.get('manifest','')
            if not isinstance(app_id,str) or not app_id or app_id in ids:raise ValueError('duplicate or invalid application id')
            ids.add(app_id)
            if not path.startswith('/system/') or path[7:] not in assets:raise ValueError('application manifest missing')
            descriptor=json.loads(assets[path[7:]])
            if descriptor.get('format')!='wishos-app/1' or descriptor.get('id')!=app_id:raise ValueError('application manifest identity')
            import posixpath
            entry=posixpath.normpath(posixpath.join(posixpath.dirname(path),descriptor.get('entry','')))
            if not entry.startswith('/system/') or not isinstance(assets.get(entry[7:]),str) or len(assets[entry[7:]].encode())>8192:raise ValueError('application entry missing or too large')
        for role in ('launcher','voice'):
            if applications.get(role) and applications[role] not in ids:raise ValueError('application role not installed')
    filesystem=b.get('runtime_filesystem')
    if filesystem is not None:
        if not isinstance(filesystem,dict) or set(filesystem)-{'format','partition','root','mount','native_mount'} or not {'format','partition','root','mount'}<=set(filesystem) or filesystem['format']!='littlefs':raise ValueError('runtime filesystem configuration')
        import posixpath
        for key in ('root','mount')+ (('native_mount',) if 'native_mount' in filesystem else ()):
            value=filesystem[key]
            if not isinstance(value,str) or not value.startswith('/') or value=='/' or posixpath.normpath(value)!=value or '..' in value.split('/'):raise ValueError('runtime filesystem path')
        if not isinstance(filesystem['partition'],str) or not filesystem['partition'] or ',' in filesystem['partition']:raise ValueError('runtime filesystem partition')
    stack=b.get('system_stack_bytes',p.get('system_stack_bytes',8192))
    if type(stack) is not int or stack<max(1024,p.get('minimum_system_stack_bytes',1024)) or stack%16 or stack>0xffffffff:raise ValueError('native system stack configuration')
    b['system_stack_bytes']=stack
    return root,b,port,p,assets

def lz4_compress(data):
    out=bytearray();table={};anchor=at=0;n=len(data)
    def length(value):
        while value>=255:out.append(255);value-=255
        out.append(value)
    while at+12<n:
        key=data[at:at+4];previous=table.get(key);table[key]=at
        if previous is None or at-previous>65535:at+=1;continue
        matched=4
        while at+matched<n-5 and data[previous+matched]==data[at+matched]:matched+=1
        literals=at-anchor;out.append((min(literals,15)<<4)|min(matched-4,15))
        if literals>=15:length(literals-15)
        out.extend(data[anchor:at]);out.extend((at-previous).to_bytes(2,'little'))
        if matched>=19:length(matched-19)
        at+=matched;anchor=at
    literals=n-anchor;out.append(min(literals,15)<<4)
    if literals>=15:length(literals-15)
    out.extend(data[anchor:]);return bytes(out)

def literal(value):
    return json.dumps(value,ensure_ascii=False)

def component_index(root, board, composition):
    repository='https://github.com/cccat6/WishOS'
    def source(path):
        try:return {'repository':repository,'path':str(path.resolve().relative_to(ROOT))}
        except ValueError:return None
    def record(identifier,layer,kind,path,description=None):
        value={'id':identifier,'layer':layer,'kind':kind}
        if (ref:=source(path)):value['source']=ref
        if description:value['description']=description
        return value
    components=[record('wishos','system','runtime',ROOT/'firmware/WishOS'),
                record(board['platform'],'driver','platform',HARDWARE/'platforms'/board['platform']),
                record(composition['id'],'driver','device',root/board['composition'])]
    for name,(directory,spec) in driver_packages(board,composition).items():
        components.append(record(name,'driver','peripheral',directory,spec.get('description')))
    directory,runtime=runtime_package(root,board)
    if runtime:
        item=record(runtime['id'],'application','startup-reference',directory)
        if '/applications.json' in asset_sources(root,board):item['catalog']='/system/applications.json'
        components.append(item)
    return {'format':'wishos-components/1','resource_discovery':'/sys/capabilities','components':components}

def generate(package, output, full_runtime=False):
    root,b,port,p,assets=load(package)
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    port_hashes={str(f.relative_to(port)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(port.rglob('*')) if f.is_file()}
    asset_hashes={k:hashlib.sha256(v.encode() if isinstance(v,str) else v).hexdigest() for k,v in assets.items()}
    drivers=driver_packages(b,p)
    driver_hashes={name:{str(f.relative_to(directory)):hashlib.sha256(f.read_bytes()).hexdigest() for f in sorted(directory.rglob('*')) if f.is_file() and '__pycache__' not in f.parts} for name,(directory,_) in drivers.items()}
    _,platform=platform_package(b)
    partition_hash=hashlib.sha256(inside(root,b['partition_file']).read_bytes()).hexdigest()
    fingerprint=hashlib.sha256(json.dumps({'board':b,'composition':p,'platform':platform,'partitions':partition_hash,'assets':asset_hashes,'composition_files':port_hashes,'drivers':driver_hashes},sort_keys=True).encode()).hexdigest()
    lines=['#pragma once','#include <stddef.h>','#include <stdint.h>','namespace notesos::boardpack {',f'inline constexpr char ID[]={literal(b["id"])};',f'inline constexpr char MCU[]={literal(b["mcu"])};',f'inline constexpr char FINGERPRINT[]={literal(fingerprint)};','struct Asset {const char*path;const char*data;size_t size;size_t stored;};','inline constexpr Asset ASSETS[]={']
    insertion=lines.index('struct Asset {const char*path;const char*data;size_t size;size_t stored;};')
    filesystem=b.get('runtime_filesystem',{})
    lines.insert(insertion,f'inline constexpr char FLASH_PARTITION[]={literal(filesystem.get("partition",""))};')
    lines.insert(insertion,f'inline constexpr char FLASH_MOUNT[]={literal(filesystem.get("mount","/storage/flash"))};')
    lines.insert(insertion,f'inline constexpr char FLASH_NATIVE_MOUNT[]={literal(filesystem.get("native_mount","/wishos-flash"))};')
    lines.insert(insertion,f'inline constexpr char RUNTIME_PATH[]={literal(filesystem.get("mount","")+filesystem.get("root",""))};')
    lines.insert(insertion,f'inline constexpr char APPLICATION_PROVIDERS[]={literal(",".join(b.get("application_providers",[])))};')
    lines.insert(insertion,f'inline constexpr size_t FLASH_BYTES={b["flash_bytes"]};')
    lines.insert(insertion,f'inline constexpr size_t SYSTEM_STACK_BYTES={b["system_stack_bytes"]};')
    _,runtime=runtime_package(root,b)
    lines.insert(insertion,f'inline constexpr char RUNTIME_ID[]={literal(runtime.get("id","board-local"))};')
    lines.insert(insertion,f'inline constexpr char DRIVERS[]={literal(",".join(drivers))};')
    lines.insert(insertion,f'inline constexpr char PLATFORM_ID[]={literal(b["platform"])};')
    lines.insert(insertion,f'inline constexpr char COMPOSITION_ID[]={literal(p["id"])};')
    lines.insert(insertion,'inline constexpr const char* CONTROL_ASSETS[]={'+','.join(literal(k[:-3] if k.endswith('.gz') else k) for k in sorted(assets) if k.startswith('/control/'))+'};')
    compiled=compiled_assets(root,b,assets)
    if full_runtime:compiled={**compiled,**assets}
    for key,value in sorted(compiled.items()):
        raw=value.encode() if isinstance(value,str) else value
        logical=len(raw);compressed=raw
        if isinstance(value,str) and (key.startswith(('/ui/','/suite/','/lib/')) or key.endswith('.lua') or key=='/components.json'):
            candidate=lz4_compress(raw)
            if len(candidate)<len(raw):compressed=candidate
        packed=compressed!=raw
        encoded=literal(value) if isinstance(value,str) and not packed else '"'+''.join(f'\\x{x:02x}' for x in compressed)+'"'
        lines.append(f'{{{literal(key)},{encoded},{logical},{len(compressed)}}},')
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
    parser=argparse.ArgumentParser();parser.add_argument('package',nargs='?',default=str(DEFAULT));parser.add_argument('--output');parser.add_argument('--full-runtime',action='store_true');a=parser.parse_args()
    if a.output:generate(a.package,a.output,a.full_runtime)
    else:load(a.package)
    print('Board package valid')
