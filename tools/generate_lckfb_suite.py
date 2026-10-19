#!/usr/bin/env python3
"""Rebuild ordinary S3 SVG-derived images and audio; authored UI JSON stays independent."""
from pathlib import Path
import json,math,subprocess,sys,xml.etree.ElementTree as ET
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'hardware/agent-os/runtime/lckfb-suite';ASSETS=BASE/'assets';BRAND=ROOT/'hardware/agent-os/runtime/brand';NS='{http://www.w3.org/2000/svg}'
PAPER='#F7F5EF';INK='#262822';MUTED='#60645B';LINE='#D9DACF';ORANGE='#A94B15'
def save(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
def svg_png(source,destination,width):
 raw=subprocess.check_output(['node',str(ROOT/'tools/render_os_svg.mjs'),str(source),str(width),'0',str(ROOT/'hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf')]);import struct
 w,h=struct.unpack('<II',raw[:8]);Image.frombytes('RGBA',(w,h),raw[8:]).save(destination,optimize=True)
def center_icon(path):
 im=Image.open(path).convert('RGBA');box=im.getchannel('A').getbbox()
 if not box:return
 dx=round((im.width-box[2]-box[0])/2);dy=round((im.height-box[3]-box[1])/2)
 if dx or dy:
  centered=Image.new('RGBA',im.size);centered.alpha_composite(im,(dx,dy));centered.save(path,optimize=True)
def icon(name,paths,color=ORANGE):
 src=ASSETS/'icons'/f'{name}.svg';src.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">{paths}</svg>');svg_png(src,src.with_suffix('.png'),28);center_icon(src.with_suffix('.png'))
for name,paths in {
 'voice':'<rect x="9" y="2" width="6" height="12" rx="3"/><path d="M5 10v2a7 7 0 0 0 14 0v-2M12 19v3M8 22h8"/>',
 'apps':'<rect x="3" y="3" width="7" height="7" rx="2"/><rect x="14" y="3" width="7" height="7" rx="2"/><rect x="3" y="14" width="7" height="7" rx="2"/><rect x="14" y="14" width="7" height="7" rx="2"/>',
 'device':'<rect x="5" y="5" width="14" height="14" rx="3"/><path d="M9 1v4M15 1v4M9 19v4M15 19v4M1 9h4M1 15h4M19 9h4M19 15h4M9 12h2l1-3 2 6 1-3h1"/>',
 'settings':'<path d="M9 3l1-2h4l1 2 3 2 2 .3 2 3.4-1 1.8v3l1 1.8-2 3.4-2 .3-3 2-1 2h-4l-1-2-3-2-2-.3-2-3.4 1-1.8v-3L2 8.7l2-3.4L6 5z"/><circle cx="12" cy="12" r="3"/>',
 'camera':'<path d="M8 5l2-3h4l2 3h4a2 2 0 0 1 2 2v13H2V7a2 2 0 0 1 2-2z"/><circle cx="12" cy="12" r="4"/>',
 'files':'<path d="M2 5h7l2 3h11v12H2zM2 8V5a2 2 0 0 1 2-2h5l2 2h9v3"/>',
 'back':'<path d="M15 4l-8 8 8 8"/>',
}.items():icon(name,paths)
svg_png(BRAND/'logo.svg',ASSETS/'brand/logo.png',64);svg_png(BRAND/'lockup.svg',ASSETS/'brand/lockup.png',208)
# Indeterminate activity uses a fixed-length moving segment. The completion
# sparkle is a separate animation, never fabricated measured progress.
for phase in ('busy','done'):
 for frame in range(12):
  if phase=='busy':
   # A genuine circle stays circular throughout rotation; the brand outline
   # remains a logo, not a distorted spinner track.
   angle=frame*30
   markup=f'<svg xmlns="http://www.w3.org/2000/svg" width="64" height="64" viewBox="0 0 64 64"><circle cx="32" cy="32" r="23" fill="none" stroke="#E5E2D8" stroke-width="4"/><circle cx="32" cy="32" r="23" fill="none" stroke="#CF6B32" stroke-width="4" stroke-linecap="round" stroke-dasharray="40 104.514" transform="rotate({angle} 32 32)"/></svg>'
   root=ET.fromstring(markup)
  else:
   root=ET.fromstring((BRAND/'loading.svg').read_text());root.attrib.update(width='64',height='64',viewBox='48 43 60 60')
   for n in list(root):
    if n.attrib.get('class')!='wish-spark':root.remove(n);continue
    f=frame/11;scale=(1.5*f/.55 if f<.55 else 1.5-(f-.55)*.5/.45);n.attrib['transform']=f'translate(78 72.62309) rotate({360*f}) scale({scale}) translate(-78 -72.62309)'
  tmp=ASSETS/'brand'/f'{phase}-{frame:02d}.svg';ET.register_namespace('','http://www.w3.org/2000/svg');tmp.write_text(ET.tostring(root,encoding='unicode'));svg_png(tmp,tmp.with_suffix('.png'),64);tmp.unlink()
if '--activity-only' in sys.argv:sys.exit(0)
# Short independently-decodable recording-ready cue, generated as ordinary data.
import struct
steps=[7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
pred=index=0;audio=bytearray();adjust=[-1,-1,-1,-1,2,4,6,8]
for block in range(4):
 data=bytearray(struct.pack('<BIhBBH',0x42,block,pred,index,0,640));codes=[]
 for i in range(640):
  t=(block*640+i)/16000;envelope=min(1,t/.012,(.16-t)/.025);target=int(2600*max(0,envelope)*math.sin(2*math.pi*880*t));delta=target-pred;code=8 if delta<0 else 0;delta=abs(delta);step=steps[index]
  for bit,v in [(4,step),(2,step>>1),(1,step>>2)]:
   if delta>=v:code|=bit;delta-=v
  diff=step*(2*(code&7)+1)>>3;pred=max(-32768,min(32767,pred+(-diff if code&8 else diff)));index=max(0,min(88,index+adjust[code&7]));codes.append(code)
 for i in range(0,640,2):data.append(codes[i]|codes[i+1]<<4)
 audio+=data
(ASSETS/'brand/ready.adpcm').write_bytes(audio)
svg_png(BRAND/'lockup.svg',ASSETS/'brand/lockup-compact.png',176)
svg_png(ASSETS/'icons/voice.svg',ASSETS/'brand/microphone.png',64)
# Asset paths are ordinary writable files, not compiled UI tables.
p=BASE/'package.json';package=json.loads(p.read_text());assets=package['assets']
for file in sorted(ASSETS.rglob('*')):
 if file.is_file()and '/recovery/'not in str(file):assets['/'+file.relative_to(ASSETS).as_posix()]='assets/'+file.relative_to(ASSETS).as_posix()
for name in ('logo.svg','loading.svg','lockup.svg'):assets['/brand/'+name]='@runtime/brand/'+name
save(p,package)
print('Generated SVG-derived images and audio:',len(assets),'files')
