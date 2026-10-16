#!/usr/bin/env python3
"""Build one board using its declared native drivers and runtime references."""
import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
from pathlib import Path
from prepare_board_package import ROOT, DEFAULT, load, stage as stage_board, driver_packages, inside
from prepare_filesystem import build as build_filesystem
from prepare_arduino import prepare_library, cli_path, cli_config, verify_cli, lock


def hook(directory, spec):
    if not spec.get('build_hook'):return None
    definition=importlib.util.spec_from_file_location('notes_driver_'+spec['id'].replace('-','_'),inside(directory,spec['build_hook']))
    module=importlib.util.module_from_spec(definition);definition.loader.exec_module(module)
    return module


def stage_sources(package, destination):
    load(package)  # Validate the whole selection before replacing generated sources.
    destination=Path(destination);destination.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent,prefix='notes-stage-') as tmp:
        candidate=Path(tmp)/'WishOS';candidate.mkdir()
        shutil.copytree(ROOT/'firmware/WishOS/src',candidate/'src')
        shutil.copy2(ROOT/'firmware/WishOS/WishOS.ino',candidate/'WishOS.ino')
        board,port=stage_board(package,candidate)
        for relative in port.get('exclude_sources',[]):
            excluded=inside(candidate/'src',relative)
            if excluded.is_dir():shutil.rmtree(excluded)
            elif excluded.exists():excluded.unlink()
        destination.mkdir(exist_ok=True)
        if (destination/'src').exists():shutil.rmtree(destination/'src')
        for file in candidate.iterdir():
            if file.is_dir():shutil.move(str(file),str(destination/file.name))
            else:shutil.copy2(file,destination/file.name)
    return board,port


def tree_digest(directory):
    digest=hashlib.sha256()
    for file in sorted(directory.rglob('*')):
        if file.is_file():digest.update(file.relative_to(directory).as_posix().encode()+b'\0'+file.read_bytes())
    return digest.hexdigest()


def build(package, stage, output):
    verify_cli()
    board,port=stage_sources(package,stage);drivers=driver_packages(board,port)
    flags=list(port.get('flags',[]));dependencies=set(port.get('dependencies',[]))
    for _,driver in drivers.values():
        flags.extend(driver.get('flags',[]));dependencies.update(driver.get('dependencies',[]))
    libraries={name:prepare_library(name) for name in sorted(dependencies)}
    native=Path(tempfile.gettempdir())/('notes-native-'+hashlib.sha256(str(stage.resolve()).encode()).hexdigest()[:12])
    if any(c.isspace() for c in str(native)):raise RuntimeError('Set TMPDIR to a path without spaces for SDK response files')
    context={'root':ROOT,'board':board,'port':port,'stage':stage,'output':output,'native_build':native,'libraries':libraries}
    hooks=[]
    for name,(directory,spec) in drivers.items():
        module=hook(directory,spec)
        if not module:continue
        hooks.append(module)
        if hasattr(module,'prepare'):
            prepared=module.prepare(context)
            for index,path in enumerate(prepared.get('libraries',[])):libraries[name+':'+str(index)]=Path(path)
            flags.extend(prepared.get('flags',[]))
    options='\n'.join(flags+['-DLV_CONF_INCLUDE_SIMPLE',f'-I"{stage}/src"'])+'\n'
    (stage/'build_opt.h').write_text(options)
    common=[cli_path(),'compile',*cli_config(),'--fqbn',port['fqbn']]
    for directory in libraries.values():common.extend(['--library',str(directory)])
    properties=dict(line.split('=',1) for line in subprocess.check_output([*common,'--show-properties=expanded',str(stage)],text=True).splitlines() if '=' in line)
    toolchain=lock()['toolchains'][port['toolchain']]
    if ':'.join(port['fqbn'].split(':')[:2])!=toolchain['id']:raise RuntimeError('Port FQBN/toolchain mismatch')
    if properties.get('version')!=toolchain['version']:
        raise RuntimeError('Arduino core version mismatch: '+properties.get('version','unknown'))
    overrides={'tools.ctags.pattern':'true','compiler.cpp.extra_flags':' '.join(flags),'upload.maximum_size':str(port['application_bytes'])}
    for module in hooks:
        if hasattr(module,'link'):overrides.update(module.link(context,properties))
    if '-flto' in flags:overrides['compiler.c.elf.extra_flags']=properties.get('compiler.c.elf.extra_flags','')+' -flto'
    command=list(common)
    for key,value in overrides.items():command.extend(['--build-property',key+'='+value])
    command.extend(['--warnings','none','--build-path',str(output),str(stage)])
    subprocess.run(command,check=True)
    image_bytes=(output/'WishOS.ino.bin').stat().st_size
    if image_bytes>port['application_bytes']:raise ValueError('Executable image exceeds declared application budget')
    filesystem=build_filesystem(package,output)
    if filesystem:
        args=output/'flash_args';args.write_text(args.read_text()+f"0x{filesystem['offset']:x} {filesystem['image']}\n")
    notices=output/'notices'
    if notices.exists():shutil.rmtree(notices)
    shutil.copytree(ROOT/'firmware/WishOS/licenses',notices/'system')
    for name,(directory,_) in drivers.items():
        for file in directory.glob('*.md'):
            dest=notices/'drivers'/name/file.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)
    for name,directory in libraries.items():
        for file in directory.iterdir():
            if file.is_file() and file.name.upper().startswith(('LICENSE','COPYING')):
                dest=notices/'dependencies'/name.replace(':','-')/file.name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(file,dest)
    git='/Library/Developer/CommandLineTools/usr/bin/git' if Path('/Library/Developer/CommandLineTools/usr/bin/git').exists() else 'git'
    revision=subprocess.check_output([git,'rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output([git,'status','--porcelain','--untracked-files=normal'],cwd=ROOT,text=True))
    manifest={'format':'notes-firmware-build/1','board':board['id'],'mcu':board['mcu'],'target':port['target'],
              'platform':port['platform_id'],'composition':port['id'],'drivers':list(drivers),'runtime':board.get('runtime'),
              'flash_bytes':board['flash_bytes'],'application_bytes':port['application_bytes'],'image_bytes':image_bytes,
              'revision':revision,'worktree_dirty':dirty,'fqbn':port['fqbn'],
              'arduino_cli':lock()['cli_version'],'arduino_core':toolchain,
              'source_sha256':tree_digest(stage/'src'),'lock_sha256':hashlib.sha256(json.dumps(lock(),sort_keys=True).encode()).hexdigest(),
              'dependencies':{name:{'directory':path.name,'sha256':tree_digest(path)} for name,path in libraries.items()},
              'files':{file.relative_to(output).as_posix():hashlib.sha256(file.read_bytes()).hexdigest() for file in sorted(output.rglob('*')) if file.is_file() and (file.is_relative_to(notices) or (file.parent==output and (file.suffix in ('.bin','.elf') or file.name in ('partitions.csv','flash_args'))))}}
    if filesystem:manifest['filesystem']=filesystem
    (output/'firmware-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    return manifest


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--board',default=os.environ.get('WISHOS_BOARD',str(DEFAULT)))
    args=parser.parse_args();package=Path(args.board)
    if not package.exists():package=ROOT/'hardware/agent-os/boards'/package
    _,board,_,_,_=load(package)
    stage=Path(os.environ.get('WISHOS_STAGE',ROOT/'build/wishos'/board['id']/'sketch/WishOS')).resolve()
    output=Path(os.environ.get('WISHOS_BUILD',ROOT/'build/wishos'/board['id']/'firmware')).resolve()
    build(package,stage,output)


if __name__=='__main__':main()
