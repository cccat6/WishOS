#!/usr/bin/env python3
"""Cross-repository real-MCP check on an explicitly selected disposable device."""
import argparse,json,subprocess,time,sys,threading
from pathlib import Path
from serial.tools.list_ports import comports
from os_client import Client
p=argparse.ArgumentParser();p.add_argument('--serial-id',required=True);p.add_argument('--hub',required=True,type=Path);p.add_argument('--node',required=True,type=Path);p.add_argument('--mode',choices=['local','cloud'],required=True);p.add_argument('--probe',action='store_true');p.add_argument('--stop-before-transfer',action='store_true',help='Explicitly request preflight-then-stop deployment policy');p.add_argument('--lan-host',help='LAN address of this host, reachable by the selected device (local mode)');p.add_argument('--output',required=True,type=Path);a=p.parse_args()
if a.mode=='local' and not a.lan_host:p.error('--lan-host is required in local mode')
ports=[p.device for p in comports() if (p.serial_number or '').upper()==a.serial_id.upper()];assert len(ports)==1
c=Client(port=ports[0],timeout=15);original=None;proc=None;report={};serial_lines=[];drain_stop=threading.Event();drainer=None
def drain_serial():
 while not drain_stop.is_set():
  line=c.serial.readline()
  if line and not line.lstrip().startswith(b'{'):
   serial_lines.append(line.decode('utf-8',errors='replace').strip()[:512]);del serial_lines[:-48]
try:
 board=c.call('/sys/board');assert board['mac']==a.serial_id.upper();original=c.call('/services/link')['desired'];end=time.monotonic()+30
 while c.call('/sys/apps').get('pending'):
  if time.monotonic()>end:raise TimeoutError('Application transition unavailable')
  time.sleep(.05)
 c.call('/sys/runtime','stop');c.call('/dev/wifi','resume_auto');end=time.monotonic()+30
 while not c.call('/dev/wifi')['connected']:
  if time.monotonic()>end:raise TimeoutError('Wi-Fi unavailable')
  time.sleep(.1)
 if not c.call('/sys/clock')['wall_clock_valid']:c.call('/sys/clock','write',{'unix_s':int(time.time())})
 err=a.output.with_suffix('.stderr.log');err.parent.mkdir(parents=True,exist_ok=True)
 with err.open('w') as stderr:
  proc=subprocess.Popen([str(a.node),'--use-env-proxy',str(Path(__file__).with_suffix('.mjs').resolve())],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=stderr,text=True)
  proc.stdin.write(json.dumps({'hub':str(a.hub.resolve()),'mode':a.mode,'serial_id':a.serial_id.upper(),'lan_host':a.lan_host,'probe':a.probe,'stop_before_transfer':a.stop_before_transfer})+'\n');proc.stdin.flush()
  for line in proc.stdout:
   try:data=json.loads(line)
   except ValueError:continue
   if data.get('phase')=='ready':
    c.call('/services/link','configure',{'settings':data['settings']});drainer=threading.Thread(target=drain_serial,daemon=True);drainer.start()
   elif data.get('phase')=='result':report=data['report'];break
  proc.wait(timeout=20)
  assert report.get('ok'),report.get('error') or err.read_text()[-2000:]
finally:
 drain_stop.set()
 if drainer:drainer.join(timeout=2)
 if serial_lines:report['serial_diagnostics']=serial_lines
 if original is not None:
  try:c.call('/services/link','configure',{'settings':original})
  except Exception as e:report['restore_error']=str(e)
 c.serial.close()
 if proc and proc.poll() is None:proc.terminate();proc.wait(timeout=15)
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2))
print(json.dumps({'ok':report.get('ok',False),'mode':a.mode,'checks':report.get('checks',[])},indent=2))
