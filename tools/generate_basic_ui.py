#!/usr/bin/env python3
"""Generate the editable factory JSON pages and small indexed, streamable UI icons."""
import json,struct
from pathlib import Path
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[1]
PACK=ROOT/'hardware/agent-os/runtime/e6-suite'
UI=PACK/'assets/ui';ICONS=PACK/'assets/icons'
UI.mkdir(exist_ok=True);ICONS.mkdir(exist_ok=True)
for name in ('network','services','storage','status','mic','moon'):
 im=Image.new('RGBA',(56,56));d=ImageDraw.Draw(im);color=(28,80,180,255)
 if name=='network':
  for box in ((5,6,51,50),(13,16,43,46),(21,27,35,41)):d.arc(box,215,325,fill=color,width=3)
  d.ellipse((25,43,31,49),fill=color)
 elif name=='services':
  for y in (10,24,38):d.rounded_rectangle((7,y,49,y+9),radius=3,outline=color,width=2);d.ellipse((12,y+3,15,y+6),fill=color)
 elif name=='storage':
  d.rounded_rectangle((8,12,48,47),radius=4,outline=color,width=3);d.line((9,21,47,21),fill=color,width=2);d.rectangle((12,7,27,12),fill=color)
 elif name=='status':
  d.rounded_rectangle((8,17,44,39),radius=4,outline=color,width=3);d.rectangle((45,24,49,32),fill=color);d.polygon((28,19,19,30,27,30,24,38,35,26,27,26),fill=color)
 elif name=='mic':
  d.rounded_rectangle((21,7,35,34),radius=7,outline=color,width=3);d.arc((14,15,42,42),0,180,fill=color,width=3);d.line((28,42,28,49),fill=color,width=3);d.line((20,49,36,49),fill=color,width=3)
 else:
  d.ellipse((9,7,45,45),fill=color);d.ellipse((24,2,52,31),fill=(0,0,0,0));d.ellipse((41,40,45,44),fill=color)
 alpha=im.getchannel('A').point(lambda v:255 if v else 0).convert('1')
 # LVGL I1: transparent + blue palette, then one bit per square pixel.
 (ICONS/(name+'.bin')).write_bytes(struct.pack('<BBHHHHH',0x19,7,0,56,56,7,0)+bytes((0,0,0,0,180,80,28,255))+alpha.tobytes())
 (ICONS/(name+'.png')).unlink(missing_ok=True)

def label(id,text,x,y,w,size=24,color='#172e56',h=None):
 node={'type':'label','id':id,'placement':{'x':x,'y':y,'width':w},'labelProps':{'text':text,'missingGlyph':'replace'},'style':{'fontSize':size,'textColor':color}}
 if h:node['placement']['height']=h
 return node

def box(id,x,y,w,h,color='#edf2ee',radius=12):
 return {'type':'container','id':id,'placement':{'x':x,'y':y,'width':w,'height':h},'style':{'bgColor':color,'radius':radius,'padding':0}}

def icon(name,x,y):return {'type':'image','placement':{'x':x,'y':y,'width':56,'height':56},'imageProps':{'src':'../icons/'+name+'.bin'}}

names={'welcome':'欢迎使用','home':'你的随身终端','menu':'设备菜单','network':'网络','services':'连接服务','storage':'内容','status':'设备状态','recording':'语音输入','sleep':'准备休眠','gallery':'彩色显示'}
for name,title in names.items():
 for wide in (False,True):
  w,h=(720,480) if wide else (480,720)
  nodes=[label('brand','AGENTIC NOTES',28,22,w-56,16),box('accent',w-111,27,82,5,'#efb321',2),label('title',title,28,64,w-56,40),label('message','',28,h-99,w-56,16,'#aa322d',48),label('keys','A 上一项   B 选择   C 下一项',28,h-45,w-56,16)]
  top=140
  if name=='welcome':
   nodes += [label('intro','连接网络，配置服务。\n也可以先使用离线内容。',28,top,w-56,24)]
   for i,(text,sub,col) in enumerate([('打开热点','A','#fff1ac'),('连接 Wi-Fi','B','#e0ebf8'),('离线使用','C','#e2efcc')]):
    x=28+i*(w-56)/3 if wide else 28;y=238 if wide else 260+i*98;cw=(w-80)/3 if wide else w-56
    node=box('choice'+str(i),int(x),y,int(cw),76,col);node['style']['borderWidth']=1;node['style']['borderColor']='#172e56';node['children']=[label('choice_label'+str(i),text,18,14,int(cw)-36,24),label('choice_key'+str(i),sub,18,48,int(cw)-36,16)];nodes.append(node)
   nodes[4]['labelProps']['text']='A 热点   B 连接   C 离线'
  elif name=='home':
   nodes += [icon('status',28,top),label('power','电池供电',99,top+3,w-130,24),label('voltage','等待电压读数',99,top+38,w-130,20)]
   nodes += [box('network_card',28,244,w-56,104,'#e0ebf8'),label('ssid','未连接',48,260,w-96,24),label('ip','192.168.4.1',48,303,w-96,20)]
   if not wide:nodes += [label('help','按任意按钮打开菜单。\n长按 B 使用语音输入。',28,409,w-56,24)]
  elif name=='menu':
   for i,text in enumerate(('网络','服务','内容','设备状态','彩色显示','休眠'),1):
    if wide:col=(i-1)//3;row=(i-1)%3;x=28+col*342;y=top+row*71;cw=322
    else:x=28;y=top+(i-1)*72;cw=w-56
    node=box('row'+str(i),x,y,cw,60,'#f6f7ef',9);node['children']=[label('item'+str(i),text,16,12,cw-32,24)];nodes.append(node)
  elif name=='network':
   nodes += [icon('network',w-87,top-9)]
   for i,(key,title_,value) in enumerate([('ssid','Wi-Fi','未连接'),('ip','后台地址','192.168.4.1'),('mac','MAC','00:00:00:00:00:00')]):
    y=top+i*(70 if wide else 114)
    nodes += [label('caption'+key,title_,28,y,w-56,16,'#687882'),label(key,value,28,y+27,w-100,24)]
   if not wide:nodes += [label('ap','设备热点',28,top+361,w-56,20)]
  elif name=='services':
   for i,(key,title_) in enumerate([('mcp','MCP'),('agent','上位 Agent'),('llm','LLM API')]):
    y=top+i*(70 if wide else 122);nodes += [label('caption'+key,title_,28,y,w-56,16),label(key,'未配置',28,y+26,w-56,20,h=68 if not wide else 44)]
   if not wide:nodes += [label('help','在设备网页中配置服务地址。\n保存地址不代表已经连接。',28,536,w-56,20)]
  elif name=='storage':
   nodes += [icon('storage',w-87,top-9),label('files','存储卡尚未就绪\n按 B 尝试挂载',28,top+22,w-95,24,h=h-top-140)]
  elif name=='status':
   for i,(key,title_,value) in enumerate([('power','供电','电池供电'),('voltage','电压','—'),('memory','可用 RAM','—'),('storage','存储卡','未挂载')]):
    y=top+i*(49 if wide else 92);nodes += [label('caption'+key,title_,28,y,168,20,'#687882'),label(key,value,194,y,w-224,24)]
  elif name=='recording':
   nodes += [icon('mic',w//2-28,top),label('capture','正在录音',28,top+94,w-56,32),label('help','松开 B 结束录音',28,top+149,w-56,24)]
  elif name=='sleep':
   nodes += [icon('moon',28,top),label('help','页面完成后进入休眠。\n按任意按钮唤醒。',104,top+3,w-132,24)]
  else:
   colors=['#bc3436','#e7bc36','#2f75b0','#85a044','#e0998a','#64a8a0']
   for i,color in enumerate(colors):
    col=i%3;row=i//3;cw=(w-76)//3;ch=76 if wide else 132
    nodes.append(box('color'+str(i),28+col*(cw+10),top+row*(ch+12),cw,ch,color,14))
   if not wide:nodes += [label('help','同一套界面，可以使用\n不同的色彩空间。',28,468,w-56,24)]
   nodes += [label('mode_help','B 切换显示驱动',28,h-135,w-56,20)]
  if name not in ('welcome','menu'):nodes[4]['labelProps']['text']='长按 A 返回   长按 B 语音'
  root={'version':'0.1.1','assets':[{'type':'viewScreen','id':name,'style':{'bgColor':'#ffffff','padding':0},'children':nodes}]}
  (UI/(name+('-wide' if wide else '')+'.json')).write_text(json.dumps(root,ensure_ascii=False,indent=2)+'\n')
board=json.loads((PACK/'package.json').read_text())
board['assets']={'/startup.lua':'assets/startup.lua','/defaults.json':'assets/defaults.json','/setup.html':'assets/setup.html'}
for folder in ('ui','suite','icons'):
 for file in sorted((PACK/'assets'/folder).glob('*')):
  board['assets']['/'+folder+'/'+file.name]=str(file.relative_to(PACK))
# Keep the ordinary manifest used by application-loader clients.
board['assets']['/default-app.json']='assets/default-app.json'
board['assets']['/lib/input.lua']='@runtime/lib/input.lua'
(PACK/'assets/default-app.json').write_text(json.dumps({'format':'notes-app/1','entry':'ui/home.json','policy_scope':'app:basic','bindings':[]},indent=2)+'\n')
(PACK/'package.json').write_text(json.dumps(board,ensure_ascii=False,indent=2)+'\n')
print('Generated',len(names)*2,'editable pages and 6 small icons')
