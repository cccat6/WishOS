#!/usr/bin/env python3
"""Snapshot the pinned LVGL sources and apply reviewed OOM corrections locally."""
import argparse,hashlib,json,shutil,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def relevant(source):
 for f in sorted(source.rglob('*')):
  if f.is_file() and not any(part.startswith('.') for part in f.relative_to(source).parts) and (f.relative_to(source).parts[0] in ('src','cmake') or f.relative_to(source).as_posix() in ('CMakeLists.txt','Kconfig','library.properties','lv_conf_template.h')):yield f
def prepare(source,manifest):
 source=Path(source).resolve();data=Path(manifest).read_bytes();spec=json.loads(data);digest=hashlib.sha256()
 for f in relevant(source):digest.update(f.relative_to(source).as_posix().encode()+b'\0'+f.read_bytes())
 if digest.hexdigest()!=spec['source_sha256']:raise RuntimeError('Library source differs from the verified snapshot; validate before upgrading')
 key=hashlib.sha256(data).hexdigest();target=ROOT/'build/dependencies'/(spec.get('name','lvgl')+'-'+key[:12]);marker=target/'.notes_snapshot'
 if marker.exists() and marker.read_text()==key:
  expected=hashlib.sha256();actual=hashlib.sha256();changes={x['file']:x for x in spec['changes']}
  for f in relevant(source):
   relative=f.relative_to(source).as_posix();data=f.read_bytes()
   if relative in changes:
    text=data.decode()
    for c in changes[relative]['replacements']:text=text.replace(c['old'],c['new'])
    data=text.encode()
   expected.update(relative.encode()+b'\0'+data)
  for f in relevant(target):actual.update(f.relative_to(target).as_posix().encode()+b'\0'+f.read_bytes())
  if expected.digest()!=actual.digest():raise RuntimeError('Pinned library output modified; do not build another task changes')
  return target
 target.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(dir=target.parent,prefix='lvgl-prepare-') as tmp:
  staged=Path(tmp)/'lvgl';shutil.copytree(source,staged,ignore=shutil.ignore_patterns('.git','examples','demos','tests','docs','__pycache__'))
  for item in spec['changes']:
   path=staged/item['file'];original=path.read_bytes()
   if hashlib.sha256(original).hexdigest()!=item['sha256']:raise RuntimeError('Unexpected library patch input: '+item['file'])
   text=original.decode()
   for change in item['replacements']:
    if text.count(change['old'])!=change['count']:raise RuntimeError('Library patch context mismatch: '+item['file'])
    text=text.replace(change['old'],change['new'])
   path.write_text(text)
  (staged/'.notes_snapshot').write_text(key)
  try:staged.rename(target)
  except FileExistsError:
   if not marker.exists() or marker.read_text()!=key:raise
 return target
