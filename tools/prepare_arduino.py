#!/usr/bin/env python3
"""Install the locked toolchain, or materialize verified library source archives."""
import argparse
import hashlib
import io
import json
import os
import shutil
import subprocess
import tempfile
import urllib.request
import zipfile
from pathlib import Path
from prepare_library_snapshot import prepare as patched_snapshot
from prepare_os_dependencies import prepare_nfs

ROOT=Path(__file__).resolve().parents[1]
LOCK=ROOT/'tools/dependencies/arduino.json'

def lock():return json.loads(LOCK.read_text())

def cli_path():
    return os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli') or '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'

def cli_config():
    return ['--config-file',os.environ['ARDUINO_CONFIG_FILE']] if os.environ.get('ARDUINO_CONFIG_FILE') else []

def verify_cli():
    version=json.loads(subprocess.check_output([cli_path(),'version','--format','json'],text=True))['VersionString']
    if version!=lock()['cli_version']:raise RuntimeError('arduino-cli version mismatch: '+version)

def prepare_library(name):
    if name=='ESP32NFS':return prepare_nfs()
    spec=lock()['libraries'][name]
    parent=ROOT/'build/dependencies/arduino';parent.mkdir(parents=True,exist_ok=True)
    archive=parent/(name+'-'+spec['version']+'.zip')
    if not archive.exists():
        data=urllib.request.urlopen(spec['url'],timeout=60).read()
        if hashlib.sha256(data).hexdigest()!=spec['sha256']:raise RuntimeError('library archive digest mismatch: '+name)
        with tempfile.NamedTemporaryFile(dir=parent,delete=False) as f:f.write(data);temporary=Path(f.name)
        temporary.replace(archive)
    data=archive.read_bytes()
    if hashlib.sha256(data).hexdigest()!=spec['sha256']:raise RuntimeError('library archive modified: '+name)
    target=parent/(name+'-'+spec['version'])
    with zipfile.ZipFile(io.BytesIO(data)) as bundle:
        files=[i for i in bundle.infolist() if not i.is_dir()]
        prefixes={Path(i.filename).parts[0] for i in files}
        if len(prefixes)!=1:raise RuntimeError('unexpected library archive layout: '+name)
        expected={}
        for info in files:
            path=Path(*Path(info.filename).parts[1:])
            if path.is_absolute() or '..' in path.parts:raise RuntimeError('invalid library archive path')
            expected[path]=bundle.read(info)
        if target.exists():
            actual={p.relative_to(target) for p in target.rglob('*') if p.is_file()}
            if actual!=set(expected) or any((target/p).read_bytes()!=raw for p,raw in expected.items()):
                raise RuntimeError('locked library source modified: '+name)
        else:
            with tempfile.TemporaryDirectory(dir=parent) as tmp:
                staged=Path(tmp)/'source';staged.mkdir()
                for path,raw in expected.items():
                    output=staged/path;output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(raw)
                staged.rename(target)
    if spec.get('patch'):return patched_snapshot(target,LOCK.parent/spec['patch'])
    return target

def install_core(toolchain):
    verify_cli();spec=lock()['toolchains'][toolchain]
    subprocess.run([cli_path(),'core','update-index',*cli_config(),'--additional-urls',spec['index_url']],check=True)
    subprocess.run([cli_path(),'core','install',spec['id']+'@'+spec['version'],*cli_config(),'--additional-urls',spec['index_url']],check=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--install-core',action='store_true');parser.add_argument('--toolchain',default='esp32');parser.add_argument('--board');parser.add_argument('--host-libraries',type=Path)
    parser.add_argument('--libraries',nargs='*',default=[]);args=parser.parse_args()
    if args.board:
        from prepare_board_package import load
        directory=Path(args.board)
        if not directory.exists():directory=ROOT/'hardware/agent-os/boards'/directory
        args.toolchain=load(directory)[3]['toolchain']
    if args.install_core:install_core(args.toolchain)
    if args.host_libraries:
        args.host_libraries.mkdir(parents=True,exist_ok=True)
        for name in lock()['libraries']:
            # Host tests apply their patch manifests themselves, so expose the verified originals.
            prepare_library(name);source=ROOT/'build/dependencies/arduino'/(name+'-'+lock()['libraries'][name]['version'])
            destination=args.host_libraries/name
            if destination.is_symlink() and destination.resolve()==source.resolve():continue
            if destination.exists() or destination.is_symlink():raise RuntimeError('host library location already occupied: '+str(destination))
            destination.symlink_to(source.resolve(),target_is_directory=True)
    for name in args.libraries:print(prepare_library(name))
