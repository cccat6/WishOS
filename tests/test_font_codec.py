import hashlib
import json
import re
import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from font_codec import pack_glyphs

class FontCodecTests(unittest.TestCase):
 def test_original_metrics_and_every_glyph_payload_are_identical(self):
  source=(ROOT/'firmware/WishOS/src/display/NativeFont.h').read_text()
  tree=[tuple(map(int,r)) for r in re.findall(r'\{(\d+),(\d+)\}',source.split('NATIVE_FONT_TREE[][2]={')[1].split('};')[0])]
  data=bytes(map(int,source.split('NATIVE_FONT_BITS[]={')[1].split('};')[0].replace('\n','').strip(',').split(',')))
  rows=[tuple(map(int,r))for r in re.findall(r'\{(\d+),(\d+),(\d+),(\d+),(-?\d+),(-?\d+),(\d+),(\d+),(\d+)\}',source)]
  digest=hashlib.sha256()
  for row in rows:
   at=row[-1]*8;decoded=bytearray()
   for _ in range(row[-2]&32767):
    node=0
    while node<256:node=tree[node][(data[at//8]>>(7-at%8))&1];at+=1
    decoded.append(node-256)
   digest.update(struct.pack('<IBBBbbBH',*row[:-1]));digest.update(decoded)
  golden=json.loads((ROOT/'tests/native-font-digest.json').read_text())
  self.assertEqual(len(rows),golden['glyphs']);self.assertEqual(digest.hexdigest(),golden['payload_sha256'])
 def test_packing_is_deterministic_and_keeps_empty_glyphs(self):
  data=b'abcabcabc';rows=[(32,16,0,0,0,0,4,0,0),(65,16,8,3,0,0,8,9,0)]
  self.assertEqual(pack_glyphs(data,rows),pack_glyphs(data,rows))
  packed,records,tree=pack_glyphs(data,rows);self.assertEqual(records[0][-2],0);self.assertEqual(records[1][-2],9)

if __name__=='__main__':unittest.main()
