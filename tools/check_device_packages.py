#!/usr/bin/env python3
"""Explicit package fault/revision checks on one disposable USB-identified device.
No Flash backup, model, screen or experiment-board access is performed.
"""
import argparse, hashlib, json, subprocess, sys, time, uuid
from pathlib import Path
from serial.tools.list_ports import comports
from os_client import Client, RpcError

p=argparse.ArgumentParser();p.add_argument('--serial-id',required=True);p.add_argument('--expected-mcu',default='ESP32-C3',choices=['ESP32-C3','ESP32-C6','ESP32-S3']);p.add_argument('--output',type=Path,required=True);p.add_argument('--esptool',type=Path);args=p.parse_args()
ports=[v.device for v in comports() if (v.serial_number or '').upper()==args.serial_id.upper()];assert len(ports)==1
client=Client(port=ports[0],timeout=15);report={'checks':[]};roots=[];app_id='check-'+uuid.uuid4().hex[:10];report['test_app_id']=app_id;copied_bytes=0;extra_apps=[];catalog_root="/storage/flash/catalog-"+uuid.uuid4().hex[:10];catalog_files=[];catalog_created=False

def trace_client(c):
    read=c.serial.readline
    def traced():
        line=read()
        if line and not line.lstrip().startswith(b'{'):
            report.setdefault('serial_diagnostics',[]).append(line.decode('utf-8',errors='replace').strip()[:512])
            report['serial_diagnostics']=report['serial_diagnostics'][-24:]
        return line
    c.serial.readline=traced
trace_client(client)
def call(path,method='read',params=None):return client.call(path,method,params)
def wait(fn,seconds=30):
    until=time.monotonic()+seconds
    while time.monotonic()<until:
        value=fn()
        if value:return value
        time.sleep(.01)
    raise TimeoutError('Package fixture condition did not settle')
def operation(path,method):
    if method=='mount':wait(lambda:not call('/sys/apps').get('pending'))
    state=wait(lambda:(r if (r:=call(path)).get('operation',{}).get('state')!='running' else None))
    if method=='mount' and state.get('mounted'):return {'ok':True}
    r=call('/sys/operations','request',{'path':path,'method':method,'args':{}});ident=r['completion_id']
    try:
        result=wait(lambda:(r if (r:=call('/sys/operations','read',{'id':ident}))['ready'] else None))
        assert result['data'].get('ok',True),result
        return result
    finally:call('/sys/operations','release',{'id':ident})
def transfer(method,params):
    until=time.monotonic()+30
    while True:
        try:return call('/sys/transfers',method,params)
        except RpcError as e:
            if str(e)=='storage_space_refresh_required':operation('/dev/flash','space')
            elif str(e) not in ('resource_busy','spi_busy','operation_active'):raise
            if time.monotonic()>until:raise
            time.sleep(.01)
def digest(data):return hashlib.sha256(data).hexdigest()
def package(code):
    files={'app.json':json.dumps({'format':'wishos-app/1','id':app_id,'title':'Package fixture','entry':'main.lua','help':{'controls':'USB fixture; system control stays reachable'}}).encode(),'main.lua':code.encode(),'kept.bin':b'unchanged data '*8192}
    inventory=''.join(json.dumps({'path':name,'bytes':str(len(data)),'sha256':digest(data)},separators=(',',':'))+'\n' for name,data in sorted(files.items())).encode()
    files['files.jsonl']=inventory;files['package.json']=json.dumps({'format':'wishos-package/1','entry':'app.json','inventory':'files.jsonl','inventory_sha256':digest(inventory)},separators=(',',':')).encode();return files

def deliver(files,base=None,old=None):
    global copied_bytes
    operation('/dev/flash','space')
    begin={'bytes':str(sum(map(len,files.values()))),'files':len(files),'request_id':'pkg-'+uuid.uuid4().hex}
    result=transfer('begin',begin);resumed=transfer('begin',begin);assert resumed['id']==result['id'] and resumed['root']==result['root']
    if 'transfer_begin_resume_keeps_identity' not in report['checks']:report['checks'].append('transfer_begin_resume_keeps_identity')
    tid,root=result['id'],result['root'];roots.append((root,files));report.setdefault('test_roots',[]).append(root)
    for name,data in files.items():
        fields={'id':tid,'path':name,'bytes':str(len(data)),'sha256':digest(data)}
        if base and old.get(name)==data:
            result=transfer('copy_file',{**fields,'source':base+'/'+name});assert result['state']=='copying'
            copied_bytes+=len(data)
            def copied():
                r=call('/sys/transfers')
                if r['state']=='aborted':raise RuntimeError(r)
                return r if r['state']=='receiving' else None
            wait(copied)
        else:
            transfer('file',fields)
            for offset in range(0,len(data),1024):transfer('chunk',{'id':tid,'offset':str(offset),'hex':data[offset:offset+1024].hex()})
            transfer('close_file',{'id':tid})
    transfer('commit',{'id':tid});transfer('release',{'id':tid});return root

def validate(root):
    result=call('/sys/apps','validate',{'manifest':root+'/app.json','package':root+'/package.json'})
    ident=result['operation_id']
    result=wait(lambda:(r if (r:=call('/sys/apps','validation',{'operation_id':ident}))['state'] in ('validated','failed') else None))
    return ident,result

def install(root,ident,revision):return call('/sys/apps','install',{'manifest':root+'/app.json','package':root+'/package.json','validation_id':ident,'expected_revision':revision,'persist':True})
def list_apps():
    entries=[];cursor=0
    while True:
        page=call('/sys/apps','list',{'cursor':cursor,'limit':4});entries.extend(page['applications'])
        if not page['more']:return entries
        cursor=page['next_cursor']
def launch():
    reply=call('/sys/operations','request',{'path':'/sys/apps','method':'launch','args':{'id':app_id}});ident=reply['completion_id']
    try:
        r=wait(lambda:(r if (r:=call('/sys/operations','read',{'id':ident}))['ready'] else None))
        assert r['event']=='launched' and r['data']['ok'],r
    finally:call('/sys/operations','release',{'id':ident})
try:
    board=call('/sys/board');report['board']=board;assert board['mcu']==args.expected_mcu and board['mac']==args.serial_id.upper()
    index=board['component_index'];parts=call('/fs','read',{'file':index['file'],'offset':'0','length':1024});raw=bytearray(bytes.fromhex(parts['hex']))
    while len(parts['hex'])==2048:
        parts=call('/fs','read',{'file':index['file'],'offset':str(len(raw)),'length':1024});raw.extend(bytes.fromhex(parts['hex']))
    components=json.loads(raw);assert components['format']=='wishos-components/1' and {c['layer'] for c in components['components']}=={'system','driver','application'}
    report['checks'].append('live_layered_component_index')
    wait(lambda:not call('/sys/apps').get('pending'));call('/sys/runtime','stop');operation('/dev/flash','mount')
    files=package("function on_start() assert(notes.call('/sys/logs','write',{message='package v1'}).ok) end")
    root=deliver(files);ident,checked=validate(root);assert checked['state']=='validated',checked;install(root,ident,checked['revision']);launch()
    runtime=call('/sys/runtime');owner=runtime['owner'];assert not runtime['fault'];report['checks'].append('native_package_validation_install_and_activation')
    try:call('/fs','write',{'file':root+'/main.lua','offset':'0','text':'bad','truncate':True});raise AssertionError('Active source was writable')
    except RpcError as e:assert str(e)=='file_in_use',e
    report['checks'].append('active_revision_blocks_in_place_write')
    before=call('/sys/apps')['revision'];bad=deliver(package('function on_start('));bad_id,bad_result=validate(bad)
    assert bad_result['state']=='failed' and bad_result['error']=='script_syntax',bad_result
    assert call('/sys/apps')['revision']==before and call('/sys/runtime')['owner']==owner
    call('/sys/apps','release_validation',{'operation_id':bad_id});report['checks'].append('fault_injected_bad_package_preserves_catalog_and_runtime')
    changed=package("function on_start() assert(notes.call('/sys/logs','write',{message='package v2'}).ok) end")
    revision=call('/sys/apps')['revision'];updated=deliver(changed,root,files);new_id,new_result=validate(updated);assert new_result['state']=='validated',new_result
    try:install(updated,new_id,revision-1);raise AssertionError('Stale catalog update accepted')
    except RpcError as e:assert str(e)=='revision_conflict',e
    assert call('/sys/runtime')['owner']==owner and call('/sys/apps')['revision']==revision
    install(updated,new_id,revision);assert call('/sys/runtime')['owner']==owner;launch();assert call('/sys/runtime')['owner']!=owner
    report['checks'].append('whole_file_update_copies_unchanged_payload_locally')
    report['checks'].append('stale_revision_rejected_before_catalog_commit')
    report['copied_payload_bytes']=copied_bytes
    # NVS strings have a smaller limit than the advertised application catalog.
    # Exercise durable catalogs beyond 4 KiB using normal install calls, then
    # verify the same records survive the reset already used below.
    call('/fs','mkdir',{'path':catalog_root});catalog_created=True;catalog_files.append(catalog_root+'/main.lua')
    call('/fs','write',{'file':catalog_files[-1],'offset':'0','text':'function on_start() end','truncate':True})
    for i in range(8):
        ident=app_id+'-catalog-'+str(i);file=catalog_root+'/'+str(i)+'.json';catalog_files.append(file)
        descriptor={'format':'wishos-app/1','id':ident,'title':'Catalog fixture '+str(i),'entry':'main.lua','source':{'repository':'https://example.invalid/reference','path':'reference/'+('x'*480),'ref':'test'}}
        call('/fs','write',{'file':file,'offset':'0','text':json.dumps(descriptor),'truncate':True});call('/fs','flush',{'file':file})
        call('/sys/apps','install',{'manifest':file,'persist':True});extra_apps.append(ident)
    assert set(extra_apps)<={a['id']for a in list_apps()};assert len(json.dumps(list_apps()).encode())>4096;report['checks'].append('durable_catalog_exceeds_4k_string_limit')
    if args.esptool:
        call('/sys/runtime','stop');operation('/dev/flash','space');pending=transfer('begin',{'bytes':str(len(changed['kept.bin'])),'files':1});roots.append((pending['root'],{'interrupted.bin':b''}))
        transfer('copy_file',{'id':pending['id'],'path':'interrupted.bin','source':updated+'/kept.bin','bytes':str(len(changed['kept.bin'])),'sha256':digest(changed['kept.bin'])})
        copying=wait(lambda:(r if (r:=call('/sys/transfers'))['state']=='copying' and 0<int(r['file_offset'])<len(changed['kept.bin']) else None))
        report['copy_offset_before_reset']=copying['file_offset']
        old_boot=client.boot_id;client.serial.close()
        subprocess.run([str(args.esptool),'--chip',{'ESP32-C3':'esp32c3','ESP32-C6':'esp32c6','ESP32-S3':'esp32s3'}[args.expected_mcu],'--port',ports[0],'run'],check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        def connected():
            matches=[p.device for p in comports() if (p.serial_number or '').upper()==args.serial_id.upper()]
            return matches[0] if len(matches)==1 else None
        port=wait(connected,20);client=Client(port=port,timeout=15);trace_client(client);new_board=call('/sys/board');assert client.boot_id!=old_boot
        assert next(a for a in list_apps() if a['id']==app_id)['manifest']==updated+'/app.json'
        assert call('/sys/transfers')['state']=='idle';operation('/dev/flash','mount');launch();assert not call('/sys/runtime')['fault']
        report['checks'].append('restart_during_copy_keeps_last_committed_package')
        assert set(extra_apps)<={a['id']for a in list_apps()};report['checks'].append('large_catalog_restores_after_reset')
    report['resources']=call('/sys/resources');report['stats']=call('/sys/stats');report['ok']=True
except Exception:
    try:report['runtime']=call('/sys/runtime');report['applications']=call('/sys/apps');report['transfer']=call('/sys/transfers');report['resources']=call('/sys/resources')
    except Exception as e:report['diagnostic_error']=str(e)
    raise
finally:
    try:
        call('/sys/runtime','stop')
        state=call('/sys/apps')
        if state.get('validation_operation_id'):call('/sys/apps','release_validation',{'operation_id':state['validation_operation_id']})
        t=call('/sys/transfers')
        if t['state'] not in ('idle','aborted'):call('/sys/transfers','abort',{'id':t['id']})
        for ident in extra_apps:call('/sys/apps','remove',{'id':ident,'persist':True})
        for file in catalog_files:
            try:call('/fs','remove',{'file':file})
            except RpcError as e:
                if str(e)!='not_found':raise
        if catalog_created:call('/fs','remove',{'file':catalog_root})
        if any(a['id']==app_id for a in list_apps()):call('/sys/apps','remove',{'id':app_id,'persist':True})
        for root,files in reversed(roots):
            for name in [*files,*(name+'.part' for name in files)]:
                try:call('/fs','remove',{'file':root+'/'+name})
                except RpcError as e:
                    if str(e)!='not_found':raise
            call('/fs','remove',{'file':root})
        call('/sys/apps','home');wait(lambda:not call('/sys/apps').get('pending'));report['cleanup_ok']=True
    except Exception as e:report['cleanup_error']=str(e)
    client.serial.close();args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2))
print(json.dumps({'ok':report.get('ok',False),'checks':report['checks'],'cleanup_ok':report.get('cleanup_ok',False)},indent=2))
