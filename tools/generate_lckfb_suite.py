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
   if n.attrib.get('class') not in ('wish-progress','wish-spark'):root.remove(n);continue
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
def label(text,x,y,w,size=16,id=None,color=INK,h=None,wrap=False):
 d={'type':'label','placement':{'x':x,'y':y,'width':w,'height':h or size+10},'style':{'fontSize':size,'lineHeight':size+5,'textColor':color},'labelProps':{'text':text,'longMode':'wrap' if wrap else 'ellipsis'}}
 if id:d['id']=id
 return d
def image(src,x,y,w,h,id=None):
 d={'type':'image','placement':{'x':x,'y':y,'width':w,'height':h},'imageProps':{'src':src,'fit':'contain'}}
 if id:d['id']=id
 return d
def button(id,text,x,y,w,h,icon=None,fill='#FFFFFF',size=18):
 children=[]
 if icon:
  n=image('../icons/'+icon+'.png',0,0,28,28);n['placement'].pop('x');n['placement'].pop('y');children.append(n)
 if text:
  n=label(text,0,0,w-20,size);n['placement']={'width':'content'};n['labelProps']['metrics']='ink';children.append(n)
 return {'type':'button','id':id,'placement':{'x':x,'y':y,'width':w,'height':h},'layout':{'type':'flex','flexFlow':'row','mainAlign':'center','crossAlign':'center','gap':10},'style':{'bgColor':fill,'borderWidth':1,'borderColor':LINE,'radius':7,'padding':0},'children':children}
def screen(children):return {'version':'0.1.1','assets':[{'type':'viewScreen','style':{'bgColor':PAPER,'padding':0},'children':children}]}
def slot(text,x,y,w,h,size=16,id=None,align='left',color=INK):
 n=label(text,0,0,w,size,id,color);n['placement']={'align':'center','width':w};n['labelProps']['metrics']='ink';n['style']['textAlign']=align
 return {'type':'container','placement':{'x':x,'y':y,'width':w,'height':h},'style':{'padding':0,'borderWidth':0,'bgOpacity':0},'children':[n]}
def header(title,subtitle=None,subtitle_id=None):
 c=[button('home','',8,4,44,44,'back',fill=PAPER),slot(title,64,4,248,28 if subtitle else 44,22)]
 if subtitle:c.append(slot(subtitle,64,32,248,18,11,subtitle_id,color=MUTED))
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
translations.update({'刷新':'Refresh','文件':'Files','本机设置':'Device options','文件共享':'File sharing','扫码进行完整设置':'Scan to configure','后台地址':'Address','同一局域网内访问':'On the same LAN','等待手机连接':'Waiting for browser','开启配网热点':'Enable setup AP','离开设置？':'Leave settings?','手机仍在连接设备设置':'A browser is connected','离开会断开手机设置会话。':'This disconnects browser settings.','再次长按用户键可强制回桌面。':'Hold user key again to force Home.','继续设置':'Stay','断开并离开':'Leave','全部类型':'All types','文件信息':'File info','点击拍照':'Tap Capture','桌面':'Home','设备状态':'Device status','设备设置':'Settings','点击开始，说完再点发送':'Tap Record, then Send when done','重新打开从15分钟开始':'Reopening resets the timer','上级':'Parent','共享':'Share','帮助':'Help','短按用户键：说话':'Press user key: talk','长按2秒：桌面':'Hold 2s: Home'})
svg_png(BRAND/'lockup.svg',ASSETS/'brand/lockup-compact.png',176)
svg_png(ASSETS/'icons/voice.svg',ASSETS/'brand/microphone.png',64)
def field(text,x,y,w,size=16,id=None,align='left',h=None,wrap=False):
 n=label(text,x,y,w,size,id,h=h,wrap=wrap);n['style']['textAlign']=align;return n
write('home',[image('../brand/lockup-compact.png',12,6,150,43),slot('未连接 Agent',168,8,140,20,12,'connection','right',MUTED),slot('',168,28,140,24,16,'clock','right',MUTED),label('今天，想做些什么？',12,55,296,16),button('voice','对话',8,84,148,48,'voice',fill='#F7E9DD'),button('apps','应用',164,84,148,48,'apps'),button('device','设备',8,140,148,48,'device'),button('settings','设置',164,140,148,48,'settings'),label('短按说话 · 长按2秒回桌面',12,204,238,12,color=MUTED),button('help','帮助',260,196,52,44,size=14)])
write('welcome',[image('../brand/logo.png',128,4,64,64),field('欢迎使用 WishOS',12,73,296,23,align='center'),field('短按用户键，与 Agent 对话',12,108,296,16,align='center'),field('触摸图标，打开应用和设置',12,136,296,16,align='center'),field('长按用户键2秒，随时回桌面',12,163,296,15,align='center'),button('ready','进入桌面',64,193,192,44,size=17)])
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'welcome{suffix}.json';d=json.loads(p.read_text());d['assets'][0]['children'][1]['id']='welcome_title';d['assets'][0]['children'][-1]['children'][-1]['id']='ready_label';save(p,d)
write('language',[image('../brand/logo.png',128,8,64,64),field('选择语言',12,82,296,22,align='center'),field('Choose your language',12,117,296,16,align='center'),button('zh','简体中文',8,174,148,52),button('en','English',164,174,148,52)])
appRows=[]
for i in range(3):
 row=button('app'+str(i),'—',8,52+i*48,304,44,'apps',size=17)
 row['children'][-1]['placement']={'width':228};row['children'][-1]['style']['textAlign']='left';row['layout']['mainAlign']='start';row['style']['paddingLeft']=12
 # Center alignment offsets are relative to the parent center.
 row['children'][0]['id']='app'+str(i)+'_icon';row['children'][-1]['id']='app'+str(i)+'_title';appRows.append(row)
write('apps',header('我的应用')+appRows+[button('previous','上一页',8,196,96,44,size=14),slot('',112,196,96,44,13,'page','center'),button('next','下一页',216,196,96,44,size=14)])
qr={'type':'qrcode','id':'qr','placement':{'x':16,'y':64,'width':128,'height':128},'qrcodeProps':{'text':'http://192.168.4.1/control/setup.html'}}
write('settings',header('设备设置')+[qr,label('扫码进行完整设置',156,58,156,15),label('Wi-Fi',156,91,156,11,color=MUTED),label('—',156,108,156,15,'ssid'),label('后台地址',156,139,156,11,color=MUTED),label('—',156,156,156,15,'address'),slot('',156,178,156,18,12,'mac',color=MUTED),button('options','本机设置',8,196,116,44,size=15),slot('等待手机连接',132,196,180,44,12,'clients','right',MUTED)])
write('settings-options',header('本机设置')+[button('ap','开启配网热点',8,52,304,44,size=17),button('language','语言 / Language',8,100,304,44,size=17),button('sharing','文件共享',8,148,304,44,size=17),label('',12,205,296,12,'hint',MUTED,h=30,wrap=True)])
write('sharing',header('文件共享')+[label('NFS · 局域网存储访问',12,59,296,18),label('—',12,99,296,15,'share_status',h=72,wrap=True),button('share_toggle','开启共享',8,190,304,44,size=17)])
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'sharing{suffix}.json';d=json.loads(p.read_text());d['assets'][0]['children'][-1]['children'][-1]['id']='share_toggle';d['assets'][0]['children'][-1]['id']='toggle';save(p,d)
write('confirm-exit',header('离开设置？')+[label('手机仍在连接设备设置',12,65,296,20),label('离开会断开手机设置会话。',12,112,296,16),label('再次长按用户键可强制回桌面。',12,146,296,13,color=MUTED),button('stay','继续设置',8,190,148,44,size=16),button('leave','断开并离开',164,190,148,44,size=16)])
write('device',header('设备状态')+[label('加速度 / g',12,56,150,14,color=MUTED),label('—',12,80,296,18,'accel'),label('角速度 / °/s',12,116,170,14,color=MUTED),label('—',12,140,296,18,'gyro'),label('—',12,186,296,14,'temperature',MUTED),label('—',12,216,296,11,'status',MUTED,h=18)])
fileRows=[]
for i in range(3):
 row=button('file'+str(i),'',8,54+i*47,304,44)
 row.pop('layout');row['children']=[slot('',10,3,282,22,15,'file'+str(i)+'_title'),slot('',10,25,282,15,10,'file'+str(i)+'_meta',color=MUTED)];fileRows.append(row)
write('files',header('文件','/','directory')+fileRows+[label('',12,68,296,16,'empty',h=72,wrap=True),button('previous','上一页',8,195,96,44,size=14),button('filter','全部类型',112,195,96,44,size=14),button('next','下一页',216,195,96,44,size=14)])
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'files{suffix}.json';d=json.loads(p.read_text());nodes=d['assets'][0]['children']
 for n in nodes[1:3]:n['placement']['width']=192;n['children'][0]['placement']['width']=192
 refresh=button('refresh','刷新' if not suffix else 'Refresh',268,4,44,44,size=12)
 nodes.append(refresh)
 for n in nodes:
  if n.get('id')=='filter':n['children'][0]['id']='filter_label'
 save(p,d)
write('file-info',header('文件信息')+[label('',12,56,296,17,'filename',h=78,wrap=True),label('',12,140,296,13,'fileinfo',h=42,wrap=True),button('previous','上一页',8,196,96,44,size=14),slot('',112,196,96,44,13,'page','center'),button('next','下一页',216,196,96,44,size=14)])
write('camera',header('相机','点击拍照','status')+[image('../icons/camera.png',8,54,304,132,'frame'),button('capture','拍照',8,192,148,44,size=17),button('save','保存到 SD',164,192,148,44,size=16)])
write('voice',header('与 Agent 对话')+[image('../brand/microphone.png',16,66,32,32,'activity'),label('说出你想做的应用',60,66,248,20,'message',h=62,wrap=True),label('点击开始，说完再点发送',12,135,296,14,'detail',MUTED,h=40,wrap=True),button('record','开始录音',8,184,304,48,'voice')])
for suffix in ('','.en'):
 p=ASSETS/'ui'/f'voice{suffix}.json';d=json.loads(p.read_text());d['assets'][0]['children'][-1]['children'][-1]['id']='record_label';save(p,d)
write('answer',header('与 Agent 对话')+[label('',12,56,296,17,'answer',h=118,wrap=True),label('',12,173,296,12,'page',MUTED,h=18),button('previous','上一页',8,196,148,44,size=16),button('next','下一页',164,196,148,44,size=16)])
# Asset paths are ordinary writable files, not compiled UI tables.
p=BASE/'package.json';package=json.loads(p.read_text());assets=package['assets']
for file in sorted(ASSETS.rglob('*')):
 if file.is_file()and '/recovery/'not in str(file):assets['/'+file.relative_to(ASSETS).as_posix()]='assets/'+file.relative_to(ASSETS).as_posix()
for name in ('logo.svg','loading.svg','lockup.svg'):assets['/brand/'+name]='@runtime/brand/'+name
save(p,package)
print('Generated UI and SVG-derived assets:',len(assets),'files')
