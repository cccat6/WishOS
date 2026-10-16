#!/usr/bin/env python3
"""Rebuild ordinary S3 launcher assets from shared SVGs and JSON page definitions."""
from pathlib import Path
import copy,json,math,os,subprocess,sys,xml.etree.ElementTree as ET
from PIL import Image
ROOT=Path(__file__).resolve().parents[1];BASE=ROOT/'hardware/agent-os/runtime/lckfb-suite';ASSETS=BASE/'assets';BRAND=ROOT/'hardware/agent-os/runtime/brand';NS='{http://www.w3.org/2000/svg}'
PAPER='#F7F5EF';INK='#262822';MUTED='#60645B';LINE='#D9DACF';ORANGE='#CF6B32'
def save(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n')
def svg_png(source,destination,width):
 raw=subprocess.check_output(['node',str(ROOT/'tools/render_os_svg.mjs'),str(source),str(width),'0',str(ROOT/'hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf')]);import struct
 w,h=struct.unpack('<II',raw[:8]);Image.frombytes('RGBA',(w,h),raw[8:]).save(destination,optimize=True)
def icon(name,paths,color=ORANGE):
 src=ASSETS/'icons'/f'{name}.svg';src.write_text(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">{paths}</svg>');svg_png(src,src.with_suffix('.png'),28)
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
  root=ET.fromstring((BRAND/'loading.svg').read_text());root.attrib['width']='64';root.attrib['height']='64'
  for n in list(root):
   if n.attrib.get('class')=='wish-progress':
    if phase=='done':root.remove(n)
    else:n.attrib.update({'stroke-dasharray':'28 72','stroke-dashoffset':str(-frame*100/12)})
   if n.attrib.get('class')=='wish-spark':
    if phase=='busy':n.attrib['opacity']='0'
    else:
     f=frame/11;scale=(1.5*f/.55 if f<.55 else 1.5-(f-.55)*.5/.45);n.attrib['transform']=f'translate(78 72.62309) rotate({360*f}) scale({scale}) translate(-78 -72.62309)'
  tmp=ASSETS/'brand'/f'{phase}-{frame:02d}.svg';ET.register_namespace('','http://www.w3.org/2000/svg');tmp.write_text(ET.tostring(root,encoding='unicode'));svg_png(tmp,tmp.with_suffix('.png'),64);tmp.unlink()
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
def label(text,x,y,w,size=16,id=None,color=INK):
 d={'type':'label','placement':{'x':x,'y':y,'width':w},'style':{'fontSize':size,'textColor':color},'labelProps':{'text':text}}
 if id:d['id']=id
 return d
def image(src,x,y,w,h,id=None):
 d={'type':'image','placement':{'x':x,'y':y,'width':w,'height':h},'imageProps':{'src':src}}
 if id:d['id']=id
 return d
def button(id,text,x,y,w,h,icon=None,fill='#FFFFFF',size=18):
 children=[]
 if icon:children.append(image('../icons/'+icon+'.png',9,(h-28)//2,28,28))
 children.append(label(text,43 if icon else 10,(h-size-8)//2,w-(52 if icon else 20),size))
 return {'type':'button','id':id,'placement':{'x':x,'y':y,'width':w,'height':h},'style':{'bgColor':fill,'borderWidth':1,'borderColor':LINE,'radius':10,'padding':0},'children':children}
def screen(children):return {'version':'0.1.1','assets':[{'type':'viewScreen','style':{'bgColor':PAPER,'padding':0},'children':children}]}
def header(title,subtitle=None):
 c=[button('home','',5,4,38,35,icon='back',fill=PAPER),label(title,50,7,258,23)]
 if subtitle:c.append(label(subtitle,12,46,296,14,color=MUTED))
 return c
def write(name,nodes):
 page=screen(nodes);save(ASSETS/'ui'/f'{name}.json',page)
 # A plain language resource variant; behavior remains the same application.
 en=copy.deepcopy(page)
 def tr(n):
  if 'labelProps'in n:n['labelProps']['text']=translations.get(n['labelProps']['text'],n['labelProps']['text'])
  for c in n.get('children',[]):tr(c)
 for node in en['assets']:tr(node)
 save(ASSETS/'ui'/f'{name}.en.json',en)
translations={'今天，想做些什么？':'What shall we make today?','对话':'Talk','应用':'Apps','设备':'Device','设置':'Settings','短按说话 · 长按2秒回桌面':'Press to talk · Hold 2s for Home','我的应用':'Your apps','点击打开应用':'Tap an app to open it','上一页':'Previous','下一页':'Next','设备与传感器':'Device & sensors','六轴姿态 · 原生驱动':'6-axis motion · Native drivers','存储卡与文件':'Files & storage','网络共享':'NFS sharing','打开网页管理文件':'Manage files in your browser','相机':'Camera','拍照':'Capture','保存到 SD':'Save to SD','正在准备相机':'Preparing camera','网络与服务':'Network & services','配网热点':'Wi-Fi setup AP','语言':'Language','与 Agent 对话':'Talk to your Agent','短按用户键开始或结束录音':'Press user key to start/stop','说出你想做的应用':'Describe the app you want','短按开始录音':'Press to record','开始录音':'Record','结束并发送':'Send recording','欢迎使用 WishOS':'Welcome to WishOS','短按用户键，与 Agent 对话':'Press the user key to talk','触摸图标，打开应用和设置':'Tap icons for apps and settings','长按用户键2秒，随时回桌面':'Hold the user key 2s for Home','进入桌面':'Open desktop','暂无应用':'No applications','选择语言':'Choose language','未连接 Agent':'Agent not connected'}
write('home',[image('../brand/lockup.png',4,0,208,59),label('未连接 Agent',215,12,100,12,'connection',MUTED),label('',253,34,64,15,'clock',MUTED),label('今天，想做些什么？',12,61,296,18),button('voice','对话',12,87,144,52,'voice',fill='#F7E9DD'),button('apps','应用',164,87,144,52,'apps'),button('device','设备',12,147,144,52,'device'),button('settings','设置',164,147,144,52,'settings'),label('短按说话 · 长按2秒回桌面',12,214,296,14,color=MUTED)])
write('welcome',[image('../brand/logo.png',128,8,64,64),label('欢迎使用 WishOS',56,79,238,23),label('短按用户键，与 Agent 对话',22,116,280,17),label('触摸图标，打开应用和设置',22,145,280,17),label('长按用户键2秒，随时回桌面',22,174,286,16),button('ready','进入桌面',80,204,160,32,size=16)])
write('language',[image('../brand/logo.png',128,12,64,64),label('选择语言',107,86,210,22),label('Choose your language',72,120,245,18,color=MUTED),button('zh','简体中文',24,169,132,46),button('en','English',164,169,132,46)])
write('apps',header('我的应用','点击打开应用')+[button('app0','对话',12,70,296,40,'voice'),button('app1','相机',12,118,296,40,'camera'),button('app2','设备',12,166,296,40,'device'),button('previous','上一页',12,213,140,25,size=13),button('next','下一页',164,213,144,25,size=13)])
# Names are model fields independent of each row's activation ID.
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'apps{suffix}.json';d=json.loads(p.read_text());
 for n in d['assets'][0]['children']:
  if n.get('id','').startswith('app'):n['children'][-1]['id']=n['id']+'_title';n['children'][0]['id']=n['id']+'_icon'
 save(p,d)
write('settings',header('网络与服务')+[label('Wi-Fi',12,47,70,14,color=MUTED),label('—',88,45,220,17,'ssid'),label('后台地址',12,78,75,14,color=MUTED),label('—',88,76,220,16,'address'),label('MAC',12,108,70,14,color=MUTED),label('—',88,106,220,15,'mac'),label('Agent',12,138,70,14,color=MUTED),label('—',88,136,220,16,'agent'),button('ap','配网热点',12,175,144,36,size=16),button('language','语言',164,175,144,36,size=16),label('—',12,218,296,12,'hint',MUTED)])
write('device',header('设备与传感器','六轴姿态 · 原生驱动')+[label('加速度 / g',12,74,130,16,color=MUTED),label('—',12,100,296,20,'accel'),label('角速度 / °/s',12,134,150,16,color=MUTED),label('—',12,160,296,20,'gyro'),label('—',12,201,296,14,'temperature',MUTED),label('—',12,222,296,12,'status',MUTED)])
write('files',header('存储卡与文件')+[label('/',50,44,256,14,'directory',MUTED)]+[button('file'+str(i),'—',12,67+i*33,296,29,size=14)for i in range(4)]+[button('parent','..',12,205,44,28,size=14),button('next','下一页',63,205,96,28,size=13),button('nfs','网络共享',166,205,142,28,size=13)])
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'files{suffix}.json';d=json.loads(p.read_text());
 for n in d['assets'][0]['children']:
  if n.get('id','').startswith('file'):n['children'][0]['id']=n['id']+'_title'
 save(p,d)
write('camera',header('相机')+[image('../brand/logo.png',128,67,64,64,'frame'),label('正在准备相机',12,140,296,16,'status'),button('capture','拍照',12,197,144,36,size=16),button('save','保存到 SD',164,197,144,36,size=16)])
# A QVGA frame fills the display below a compact action overlay; data remains a
# transient RAM file, and saving is an explicit user operation.
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'camera{suffix}.json';d=json.loads(p.read_text());node=d['assets'][0]['children'].pop(2);node['placement']={'x':0,'y':0,'width':320,'height':240};d['assets'][0]['children'].insert(0,node);save(p,d)
write('voice',header('与 Agent 对话')+[image('../brand/logo.png',128,52,64,64,'activity'),label('说出你想做的应用',20,125,280,20,'message'),label('短按用户键开始或结束录音',20,157,280,14,'detail',MUTED),button('record','开始录音',12,193,296,40,'voice')])
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'voice{suffix}.json';d=json.loads(p.read_text());d['assets'][0]['children'][-1]['children'][-1]['id']='record_label';save(p,d)
write('answer',header('与 Agent 对话')+[label('',12,54,296,18,'answer'),label('',12,177,296,13,'page',MUTED),button('previous','上一页',12,203,144,34,size=15),button('next','下一页',164,203,144,34,size=15)])
# Keep camera controls readable above a full-size frame.
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'camera{suffix}.json';d=json.loads(p.read_text());children=d['assets'][0]['children'];children.insert(1,{'type':'container','placement':{'x':0,'y':0,'width':320,'height':43},'style':{'bgColor':PAPER,'padding':0}});children.insert(2,{'type':'container','placement':{'x':0,'y':132,'width':320,'height':108},'style':{'bgColor':PAPER,'padding':0,'bgOpacity':210}});save(p,d)
# Asset paths are ordinary writable files, not compiled UI tables.
p=BASE/'package.json';package=json.loads(p.read_text());assets=package['assets']
for file in sorted(ASSETS.rglob('*')):
 if file.is_file()and '/recovery/'not in str(file):assets['/'+file.relative_to(ASSETS).as_posix()]='assets/'+file.relative_to(ASSETS).as_posix()
for name in ('logo.svg','loading.svg','lockup.svg'):assets['/brand/'+name]='@runtime/brand/'+name
save(p,package)
print('Generated UI and SVG-derived assets:',len(assets),'files')
