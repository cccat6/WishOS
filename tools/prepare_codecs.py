#!/usr/bin/env python3
"""Build a hash-verified ES7210/ES8311-only snapshot, with no vendor bus stack."""
import hashlib,json,shutil,tempfile,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def prepare():
 spec=json.loads((ROOT/'tools/dependencies/esp-codecs.json').read_text());key=hashlib.sha256(json.dumps(spec,sort_keys=True).encode()).hexdigest();target=ROOT/'build/dependencies'/('WishCodecs-'+key[:12]);cache=ROOT/'build/dependencies/codec-input';expected={}
 for item in spec['files']:
  path=cache/item['path'];path.parent.mkdir(parents=True,exist_ok=True)
  if not path.exists():path.write_bytes(urllib.request.urlopen(f'https://raw.githubusercontent.com/{spec["repository"]}/{spec["commit"]}/{spec["directory"]}/{item["path"]}',timeout=30).read())
  raw=path.read_bytes()
  if hashlib.sha256(raw).hexdigest()!=item['sha256']:raise RuntimeError('Codec snapshot changed: '+item['path'])
  name=Path(item['path']).name;name=name if name=='LICENSE' else 'src/'+name
  if name in expected:raise RuntimeError('Codec source name collision')
  expected[name]=raw
 expected['library.properties']=b'name=WishCodecs\nversion=1.0.0\nauthor=Espressif\nmaintainer=WishOS\nsentence=Pinned ES7210 and ES8311 codec implementations\nparagraph=Uses WishOS shared I2C arbitration\ncategory=Signal Input/Output\narchitectures=esp32\n'
 expected['src/WishCodecs.h']=b'#pragma once\n#include "es7210_adc.h"\n#include "es8311_codec.h"\n#include "audio_codec_ctrl_if.h"\n'
 if target.exists():
  for name,raw in expected.items():
   if not (target/name).is_file()or(target/name).read_bytes()!=raw:raise RuntimeError('Prepared codec snapshot modified: '+name)
  return target
 with tempfile.TemporaryDirectory(dir=target.parent) as temp:
  stage=Path(temp)/'library';stage.mkdir()
  for name,raw in expected.items():p=stage/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
  stage.rename(target)
 return target
