#!/usr/bin/env python3
"""Prepare namespaced radio callbacks in build-local SDK archives; installed SDK stays intact."""
import argparse,hashlib,json,shlex,subprocess
from pathlib import Path
CALLBACKS=['receive_done','transmit_done','transmit_failed','receive_sfd_done','transmit_sfd_done','energy_detect_done','cca_done']
def prepare(sdk:Path,toolbin:Path,out:Path):
    if sdk.name!='3.3.11':raise RuntimeError('Radio callback ABI validated only for Arduino ESP32 SDK 3.3.11')
    out.mkdir(parents=True,exist_ok=True)
    records={}
    for archive,prefix,names in [('libopenthread.a','notes_thread',CALLBACKS+['receive_at_done','enh_ack_generator']),('libzboss_port.native.a','notes_zigbee',CALLBACKS)]:
        src=sdk/'lib'/archive;dest=out/archive
        defined=subprocess.run([str(toolbin/'riscv32-esp-elf-nm'),'-g','--defined-only',str(src)],capture_output=True,text=True,check=True).stdout
        actual={line.split()[-1] for line in defined.splitlines() if ' T esp_ieee802154_' in line}
        expected={'esp_ieee802154_'+name for name in names}
        if actual!=expected:raise RuntimeError(f'Unexpected callback ABI in {archive}: {actual^expected}')
        mapping=out/(archive+'.symbols');mapping.write_text(''.join(f'esp_ieee802154_{n} {prefix}_{n}\n' for n in names))
        digest=hashlib.sha256(src.read_bytes()+mapping.read_bytes()).hexdigest()
        cache=out/(archive+'.sha256')
        receipt={}
        if cache.exists():
            try:receipt=json.loads(cache.read_text())
            except json.JSONDecodeError:pass
        if receipt.get('input_sha256')==digest and dest.exists():
            if receipt.get('output_sha256')!=hashlib.sha256(dest.read_bytes()).hexdigest():
                raise RuntimeError('Prepared radio archive modified: '+archive)
        else:
            subprocess.run([str(toolbin/'riscv32-esp-elf-objcopy'),'--redefine-syms',str(mapping),str(src),str(dest)],check=True)
            cache.write_text(json.dumps({'input_sha256':digest,'output_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}))
        records[archive]={'input_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'prepared_sha256':hashlib.sha256(dest.read_bytes()).hexdigest()}
    # Replace every occurrence, retaining the SDK's original archive ordering and repeats.
    tokens=shlex.split((sdk/'flags/ld_libs').read_text())
    if '-lopenthread' not in tokens:raise RuntimeError('OpenThread link entry missing')
    response=out/'ld_libs';response.write_text(' '.join('"'+str(out/'libopenthread.a')+'"' if x=='-lopenthread' else x for x in tokens))
    (out/'manifest.json').write_text(json.dumps({'sdk':str(sdk),'archives':records},indent=2)+'\n')
    return response
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--sdk',type=Path,required=True);p.add_argument('--toolbin',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(prepare(a.sdk.resolve(),a.toolbin.resolve(),a.output.resolve()))
