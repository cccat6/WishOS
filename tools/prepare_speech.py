#!/usr/bin/env python3
"""Build a verified WakeNet-only Arduino library; no ASR/TTS/cloud application."""
import hashlib,json,shutil,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MANIFEST=ROOT/'tools/dependencies/esp-sr.json'
def prepare(target='esp32c6'):
    spec=json.loads(MANIFEST.read_text());commit=spec['commit']
    # This manifest is for the C6 archive ABI. Other targets require a validated
    # archive snapshot; do not silently select a similarly named vendor library.
    if target!='esp32c6':raise ValueError('WakeNet snapshot target not validated: '+target)
    raw=ROOT/'build/dependencies'/('esp-sr-'+commit[:12]);raw.mkdir(parents=True,exist_ok=True)
    for f in spec['files']:
        p=raw/f['path'];p.parent.mkdir(parents=True,exist_ok=True)
        if not p.exists():p.write_bytes(urllib.request.urlopen(f'https://raw.githubusercontent.com/espressif/esp-sr/{commit}/{f["path"]}',timeout=60).read())
        if hashlib.sha256(p.read_bytes()).hexdigest()!=f['sha256']:raise ValueError('Speech dependency changed: '+f['path'])
    fft_manifest=ROOT/'tools/dependencies/dl-fft.json';fft=json.loads(fft_manifest.read_text());fft_root=ROOT/'build/dependencies'/('dl-fft-'+fft['commit'][:12])
    for f in fft['files']:
        p=fft_root/f['path'];p.parent.mkdir(parents=True,exist_ok=True)
        if not p.exists():p.write_bytes(urllib.request.urlopen(f'https://raw.githubusercontent.com/espressif/esp-dl/{fft["commit"]}/{f["path"]}',timeout=60).read())
        if hashlib.sha256(p.read_bytes()).hexdigest()!=f['sha256']:raise ValueError('FFT dependency changed: '+f['path'])
    key=hashlib.sha256(MANIFEST.read_bytes()+fft_manifest.read_bytes()+Path(__file__).read_bytes()).hexdigest()
    library=ROOT/'build/dependencies'/('NotesWakeNet-'+key[:12]);src=library/'src';src.mkdir(parents=True,exist_ok=True)
    (library/'library.properties').write_text('name=NotesWakeNet\nversion=2.4.2\nauthor=Espressif\nmaintainer=AgenticOS\nsentence=Compiled WakeNet backend\nparagraph=Verified ESP-SR wake word implementation without application logic\ncategory=Data Processing\nurl=https://github.com/espressif/esp-sr\narchitectures=esp32\nprecompiled=true\n')
    for header in (raw/'include'/target).glob('*.h'):shutil.copy2(header,src/header.name)
    shutil.copy2(raw/'src/include/model_path.h',src/'model_path.h');shutil.copy2(raw/'LICENSE',library/'LICENSE')
    shutil.copy2(raw/'src/esp_sr_debug.c',src/'esp_sr_debug.c')
    shutil.copy2(raw/'src/include/esp_sr_debug.h',src/'esp_sr_debug.h')
    for f in fft['files']:
        p=fft_root/f['path']
        if p.suffix in ('.h','.c'):shutil.copy2(p,src/p.name)
    arch=src/target;arch.mkdir(exist_ok=True)
    for archive in (raw/'lib'/target).glob('*.a'):shutil.copy2(archive,arch/archive.name)
    model=raw/'model/wakenet_model/wn9s_hiesp';files=sorted(model.iterdir());lines=['#include "model_path.h"']
    for i,p in enumerate(files):lines.append('alignas(16) static const unsigned char file_%d[]={%s};'%(i,','.join(str(b) for b in p.read_bytes())))
    lines+=['static char name[]="wn9s_hiesp";','static char*names[]={name};',
      'static char*files[]={'+','.join('const_cast<char*>("'+p.name+'")' for p in files)+'};',
      'static char*data[]={'+','.join('reinterpret_cast<char*>(const_cast<unsigned char*>(file_'+str(i)+'))' for i in range(len(files)))+'};',
      'static int sizes[]={'+','.join(str(p.stat().st_size) for p in files)+'};',
      'static srmodel_data_t model={'+str(len(files))+',files,data,sizes};','static srmodel_data_t*models[]={&model};',
      'static char infoValue[]='+json.dumps((model/'_MODEL_INFO_').read_text().strip())+';static char*info[]={infoValue};',
      'extern "C" srmodel_list_t*get_static_srmodels(){static srmodel_list_t list{};list.model_name=names;list.model_info=info;list.num=1;list.model_data=models;return &list;}',
      'extern "C" char*get_model_base_path(){static char empty[]="";return empty;}']
    (src/'NotesWakeModels.cpp').write_text('\n'.join(lines)+'\n');(src/'NotesWakeModels.h').write_text('#pragma once\n#include "esp_wn_models.h"\n')
    return library
if __name__=='__main__':print(prepare())
