#!/usr/bin/env python3
"""Build a managed LittleFS factory directory, separate from executable firmware."""
import hashlib
import io
import json
import platform
import os
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from prepare_board_package import ROOT, load


def tool():
    spec=json.loads((ROOT/'tools/dependencies/filesystem.json').read_text())
    cc=os.environ.get('CC') or shutil.which('clang') or shutil.which('cc')
    cxx=os.environ.get('CXX') or shutil.which('clang++') or shutil.which('c++')
    if platform.system()=='Darwin':
        cc=os.environ.get('CC','/Library/Developer/CommandLineTools/usr/bin/clang');cxx=os.environ.get('CXX','/Library/Developer/CommandLineTools/usr/bin/clang++')
    compiler=subprocess.check_output([cxx,'--version'],text=True).splitlines()[0]
    key=hashlib.sha256(json.dumps(spec,sort_keys=True).encode()+Path(__file__).read_bytes()+compiler.encode()+platform.machine().encode()).hexdigest()
    base=ROOT/'build/dependencies/mklittlefs'/key;base.mkdir(parents=True,exist_ok=True)
    binary=base/('mklittlefs.exe' if platform.system()=='Windows' else 'mklittlefs');receipt=base/'binary.sha256'
    if binary.exists():
        if not receipt.exists() or hashlib.sha256(binary.read_bytes()).hexdigest()!=receipt.read_text():raise RuntimeError('LittleFS builder modified')
        return binary
    with tempfile.TemporaryDirectory(dir=base) as temporary:
        stage=Path(temporary)
        for name,source in spec['sources'].items():
            archive=base/(name+'.tar.gz')
            if not archive.exists():archive.write_bytes(urllib.request.urlopen(source['url'],timeout=60).read())
            raw=archive.read_bytes()
            if hashlib.sha256(raw).hexdigest()!=source['sha256']:raise RuntimeError('LittleFS source digest mismatch')
            with tarfile.open(fileobj=io.BytesIO(raw)) as bundle:
                for member in bundle.getmembers():
                    if not member.isfile():continue
                    path=Path(*Path(member.name).parts[1:])
                    if path.is_absolute() or '..' in path.parts:raise RuntimeError('LittleFS source path')
                    target=stage/name/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(bundle.extractfile(member).read())
        root=stage/'builder';shutil.copytree(stage/'littlefs',root/'littlefs',dirs_exist_ok=True)
        main=root/'main.cpp';text=main.read_text()
        for old,new in [('s_cfg.cache_size = 64;',f's_cfg.cache_size = {spec["cache_bytes"]};'),('s_cfg.lookahead_size = 64;',f's_cfg.lookahead_size = {spec["lookahead_bytes"]};')]:
            if text.count(old)!=1:raise RuntimeError('LittleFS builder patch context')
            text=text.replace(old,new)
        # The upstream extractor puts the entire file on its stack. Stream
        # large resources so a complete font does not exhaust the host stack.
        old = '    uint8_t buffer[littlefsFile->size];'
        if text.count(old) != 1:raise RuntimeError('LittleFS extraction buffer context')
        text = text.replace(old, '    uint8_t buffer[4096];')
        start = text.index('    // read content into buffer', text.index('uint8_t buffer[4096];'))
        end = text.index('    // Adjust time, if present', start)
        text = text[:start] + """    FILE* dst = fopen(destPath, "wb");
    if (!dst) { lfs_file_close(&s_fs, &src); return false; }
    uint32_t remaining = littlefsFile->size;
    bool copied = true;
    while (remaining) {
        size_t request = std::min<size_t>(sizeof buffer, remaining);
        lfs_ssize_t got = lfs_file_read(&s_fs, &src, buffer, request);
        if (got <= 0 || fwrite(buffer, 1, got, dst) != size_t(got)) { copied = false; break; }
        remaining -= got;
    }
    lfs_file_close(&s_fs, &src);
    if (fclose(dst)) copied = false;
    if (!copied) return false;

""" + text[end:]
        main.write_text(text)
        flags=['-Os','-DLFS_NAME_MAX=255','-DLFS_ATTR_MAX=1022']
        if platform.system()=='Darwin':
            sdk=os.environ.get('SDK') or next(str(p) for p in [Path('/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk'),Path('/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk')] if p.exists())
            flags+=['-isysroot',sdk]
        objects=[]
        for source in ['lfs.c','lfs_util.c']:
            obj=stage/(source+'.o');subprocess.run([cc,*flags,'-std=c99','-c',str(root/'littlefs'/source),'-o',str(obj)],check=True);objects.append(str(obj))
        subprocess.run([cxx,*flags,'-std=c++17','-DVERSION="4.0.2-wishos"','-DBUILD_CONFIG_NAME="esp32-cache512"','-DLITTLEFS_VERSION="d01280e"','-DBUILD_CONFIG=""','-Itclap','-Iinclude','-Ilittlefs','-I.','main.cpp',*objects,'-o',str(binary)],cwd=root,check=True)
    receipt.write_text(hashlib.sha256(binary.read_bytes()).hexdigest());return binary


def partition(package,board):
    from flash_layout import validate
    entry=next(p for p in validate(package,board) if p['name']==board['runtime_filesystem']['partition'])
    return {key:entry[key] for key in ('offset','bytes')}


def build(package,output):
    directory,board,_,_,assets=load(package)
    if not board.get('runtime_filesystem'):return None
    config=board['runtime_filesystem'];layout=partition(directory,board);output=Path(output);output.mkdir(parents=True,exist_ok=True)
    image=output/'runtime.littlefs.bin';builder=tool()
    with tempfile.TemporaryDirectory() as temporary:
        source=Path(temporary)/'source';source.mkdir();expected={}
        for name,value in assets.items():
            # Native startup metadata has a single compiled authority. Do not
            # ship writable aliases that are never read during normal startup.
            if name in ('/components.json','/defaults.json','/applications.json','/services.json'):continue
            relative=config['root'].lstrip('/')+name
            raw=value.encode() if isinstance(value,str) else value
            target=source/relative;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw);expected[relative]=hashlib.sha256(raw).hexdigest()
        for file in source.rglob('*'):os.utime(file,(0,0))
        listing=Path(temporary)/'files.txt';listing.write_text(''.join('/'+name+'\n' for name in sorted(expected)))
        args=['-b','4096','-p','256','-s',str(layout['bytes'])]
        subprocess.run([str(builder),'-c',str(source),'-T',str(listing),*args,str(image)],check=True,stdout=subprocess.DEVNULL)
        extracted=Path(temporary)/'verified';extracted.mkdir()
        subprocess.run([str(builder),'-u',str(extracted),*args,str(image)],check=True,stdout=subprocess.DEVNULL)
        actual={str(p.relative_to(extracted)):hashlib.sha256(p.read_bytes()).hexdigest() for p in extracted.rglob('*') if p.is_file()}
        if actual!=expected:raise RuntimeError('Factory filesystem roundtrip differs: '+str([name for name in set(actual)|set(expected) if actual.get(name)!=expected.get(name)]))
    return {**config,**layout,'image':image.name,'files':expected,'tool_version':'4.0.2','initial_flash_only':True}
