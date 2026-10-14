#!/usr/bin/env python3
from pathlib import Path
from prepare_library_snapshot import prepare as snapshot
def prepare(source):return snapshot(source,Path(__file__).parent/'patches/lvgl-9.3.0-memory.json')
if __name__=='__main__':
 import sys
 print(prepare(sys.argv[1]))
