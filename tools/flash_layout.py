"""Validate a device's declared Flash and partition budget before compilation."""
import csv
import re
from pathlib import Path


def number(value):
    value=value.strip();suffix=value[-1:].upper()
    return int(value[:-1] if suffix in ('K','M') else value,0)*({'K':1024,'M':1048576}.get(suffix,1))


def partitions(path):
    result=[];end=0x9000
    for fields in csv.reader(Path(path).read_text().splitlines()):
        if not fields or not fields[0].strip() or fields[0].lstrip().startswith('#'):continue
        if len(fields)<5:raise ValueError('partition fields')
        name,kind,subtype,offset,size=(s.strip() for s in fields[:5])
        app=kind in ('app','0','0x00');alignment=0x10000 if app else 0x1000
        start=number(offset) if offset else (end+alignment-1)//alignment*alignment
        length=number(size)
        if start%alignment or length<=0 or length%4096:raise ValueError('partition alignment or size')
        if start<end or any(p['name']==name for p in result):raise ValueError('partition overlap or duplicate')
        result.append({'name':name,'type':kind,'subtype':subtype,'offset':start,'bytes':length,'app':app});end=start+length
    return result


def validate(package,board):
    layout=partitions(Path(package)/board['partition_file'])
    if not layout or max(p['offset']+p['bytes'] for p in layout)>board['flash_bytes']:raise ValueError('partition exceeds declared Flash')
    apps=[p for p in layout if p['app']]
    capacity=board['application_bytes']
    if type(capacity) is not int or capacity<=0 or not apps or capacity>min(p['bytes'] for p in apps):raise ValueError('application budget exceeds partition')
    size=re.search(r'(?:[:,])FlashSize=(\d+)M(?:,|$)',board['fqbn'])
    if not size or int(size[1])*1048576!=board['flash_bytes']:raise ValueError('FQBN Flash capacity mismatch')
    filesystem=board.get('runtime_filesystem')
    if filesystem:
        matches=[p for p in layout if p['name']==filesystem['partition'] and p['type'] in ('data','1','0x01') and p['subtype'] in ('spiffs','0x82','130')]
        if len(matches)!=1:raise ValueError('runtime LittleFS partition required')
    return layout
