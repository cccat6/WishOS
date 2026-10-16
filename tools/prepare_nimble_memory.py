#!/usr/bin/env python3
"""Select the SDK's supported external-memory NimBLE allocation implementation.

The Arduino SDK ships its internal-memory object in libbt.a. Linking our explicit
bridge selects a separately compiled copy of that same source, using its official
EXTERNAL configuration branch. No duplicate-symbol suppression or ABI change.
"""
import hashlib,json,tempfile,urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def prepare():
 spec=json.loads((ROOT/'tools/dependencies/nimble-memory.json').read_text())
 prefix=b'''#include <stdbool.h>
#include <sdkconfig.h>
#include <esp_idf_version.h>
#if ESP_IDF_VERSION != ESP_IDF_VERSION_VAL(5,5,5)
#error "Revalidate NimBLE allocator against the selected SDK before upgrading"
#endif
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_INTERNAL
#undef CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_IRAM_8BIT
#define CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL 1
'''
 files={}
 cache=ROOT/'build/dependencies/nimble-memory-input';cache.mkdir(parents=True,exist_ok=True)
 for item in spec['files']:
  p=cache/Path(item['path']).name
  if not p.exists():p.write_bytes(urllib.request.urlopen(f'https://raw.githubusercontent.com/{spec["repository"]}/{spec["commit"]}/{item["path"]}',timeout=30).read())
  raw=p.read_bytes()
  if hashlib.sha256(raw).hexdigest()!=item['sha256']:raise ValueError('NimBLE dependency differs: '+item['path'])
  if item['path']=='LICENSE':files['LICENSE']=raw;continue
  files['src/WishNimbleMemory.c']=prefix+raw+b'\nbool wishos_nimble_external_memory(void){return heap_caps_get_total_size(MALLOC_CAP_SPIRAM)>0;}\n'
 files['src/WishNimbleMemory.h']=b'#pragma once\n#include <stdbool.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\nbool wishos_nimble_external_memory(void);\n#ifdef __cplusplus\n}\n#endif\n'
 files['library.properties']=b'name=WishNimbleMemory\nversion=1.0.0\nauthor=Espressif\nmaintainer=WishOS\nsentence=SDK NimBLE host PSRAM allocator selection\nparagraph=Controller and native task stacks remain in internal RAM\ncategory=Communication\narchitectures=esp32\n'
 key=hashlib.sha256(Path(__file__).read_bytes()+json.dumps(spec,sort_keys=True).encode()).hexdigest();target=cache.parent/('WishNimbleMemory-'+key[:12])
 if target.exists():
  actual={str(p.relative_to(target)):p.read_bytes()for p in target.rglob('*')if p.is_file()}
  if actual!=files:raise ValueError('Prepared NimBLE snapshot modified')
  return target
 with tempfile.TemporaryDirectory(dir=cache.parent)as temporary:
  stage=Path(temporary)/'library'
  for name,raw in files.items():p=stage/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(raw)
  stage.rename(target)
 return target
if __name__=='__main__':print(prepare())
