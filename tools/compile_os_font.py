#!/usr/bin/env python3
"""Compile a compressed native glyph subset for the selected runtime package."""
import argparse,json,re,struct
from pathlib import Path
import lz4.block
from native_font import StemFont
from font_codec import header
p=argparse.ArgumentParser();p.add_argument('--font',type=Path,required=True);p.add_argument('--package',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--format',choices=['header','nmf'],default='header');p.add_argument('--sizes',default='16,20,24,32,40,48,60');a=p.parse_args()
a.output.parent.mkdir(parents=True,exist_ok=True)
chars=set(map(chr,range(32,127)))|{'□','…','—'}
for file in (a.package/'assets').rglob('*'):
 if file.suffix in ('.json','.lua'):chars.update(ch for ch in file.read_text() if ord(ch)>255)
blob=bytearray();glyphs=[]
for size in map(int,a.sizes.split(',')):
 font=StemFont(a.font,size)
 for ch in sorted(chars):
  mask,x,y,advance=font.glyph(ch);w,h=mask.size if mask else (0,0);y+=round(size*.88)
  raw=mask.tobytes() if mask else b''
  if w>96 or h>96 or len(raw)>(512 if a.format=='header' else 1152):raise ValueError('glyph exceeds native decoder scratch')
  packed=lz4.block.compress(raw,mode='high_compression',compression=9,store_size=False) if raw else b''
  compressed=a.format=='header' and len(packed)<len(raw);stored=packed if compressed else raw
  glyphs.append((ord(ch),size,w,h,x,y,advance,len(stored)|(0x8000 if compressed else 0),len(blob)));blob+=stored
if a.format=='nmf':
 a.output.write_bytes(b'NMF1'+struct.pack('<III',len(glyphs),len(blob),0)+b''.join(struct.pack('<IBBBbbBHI',*g) for g in glyphs)+blob)
 print(json.dumps({'characters':len(chars),'glyphs':len(glyphs),'bytes':a.output.stat().st_size}));raise SystemExit(0)
a.output.write_text(header(blob,glyphs))
print(json.dumps({'characters':len(chars),'glyphs':len(glyphs),'original_payload_bytes':len(blob),'header_bytes':a.output.stat().st_size}))
