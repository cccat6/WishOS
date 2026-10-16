#!/usr/bin/env python3
"""Package exactly one verified build; never flash or upload anything."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path

def package(build, output):
    build=Path(build);manifest=json.loads((build/'firmware-manifest.json').read_text())
    if manifest.get('format')!='notes-firmware-build/1':raise ValueError('build manifest required')
    files={}
    for name,digest in manifest['files'].items():
        if Path(name).is_absolute() or '..' in Path(name).parts:raise ValueError('invalid artifact filename')
        raw=(build/name).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=digest:raise ValueError('artifact differs from compiled manifest: '+name)
        if name.endswith('.bin') or name in ('partitions.csv','flash_args') or name.startswith('notices/'):files[name]=raw
    required={'WishOS.ino.bin','WishOS.ino.bootloader.bin','WishOS.ino.partitions.bin','boot_app0.bin','partitions.csv','flash_args'}
    if not required.issubset(files):raise ValueError('incomplete firmware set')
    if manifest.get('filesystem') and manifest['filesystem']['image'] not in files:raise ValueError('factory filesystem image missing')
    files['firmware-manifest.json']=(build/'firmware-manifest.json').read_bytes()
    files['README.txt']=(
        f"Board: {manifest['board']}\nMCU: {manifest['mcu']}\nRevision: {manifest['revision']}\n"
        "This archive is for this board only. A successful build is not physical validation.\n"
        "flash_args records the compiler's addresses and Flash settings. Verify device identity\n"
        "and partition layout before flashing. Full-image flashing replaces the partition table;\n"
        "application-only updates require an already compatible table. No automatic flashing occurs.\n"
        "runtime.littlefs.bin is an INITIAL factory filesystem. Writing it replaces existing files.\n"
        "Normal firmware updates must preserve it; update application packages through /fs or MCP.\n"
        "See notices/ and the repository for source/provenance and dependency licenses.\n"
    ).encode()
    files['release-manifest.json']=json.dumps({'format':'notes-firmware-release/1','board':manifest['board'],
        'files':{name:hashlib.sha256(raw).hexdigest() for name,raw in files.items()}},indent=2).encode()+b'\n'
    output=Path(output);output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'w',compression=zipfile.ZIP_DEFLATED) as archive:
        for name,raw in sorted(files.items()):
            info=zipfile.ZipInfo(manifest['board']+'/'+name,date_time=(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_DEFLATED
            archive.writestr(info,raw)
    return output

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('build',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();print(package(args.build,args.output))
