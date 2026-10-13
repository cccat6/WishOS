#pragma once
#include <stddef.h>
#include <stdint.h>
#define NOTES_HAS_VBUS_ADC 0
namespace notesos::boardpack {
inline constexpr char ID[]="e6-nova-c6";
inline constexpr char MCU[]="ESP32-C6FH4";
inline constexpr char FINGERPRINT[]="7f95df07fa4269ad95989d8e192d02412e8b3c9bd033f5453be2980b35a4d0e6";
inline constexpr char STARTUP_SCRIPT[]="/system/startup.lua";
struct Asset {const char*path;const char*data;};
inline constexpr Asset ASSETS[]={
{"/default-app.json","{\"format\":\"notes-app/1\",\"entry\":\"ui/home.json\",\"bindings\":[{\"source\":\"/dev/buttons\",\"event\":\"press\",\"where\":{\"button\":\"B\"},\"target\":\"/dev/mic\",\"method\":\"start\",\"args\":{},\"cooldown_ms\":0},{\"source\":\"/dev/buttons\",\"event\":\"release\",\"where\":{\"button\":\"B\"},\"target\":\"/dev/mic\",\"method\":\"stop\",\"args\":{},\"cooldown_ms\":0},{\"source\":\"/dev/buttons\",\"event\":\"press\",\"where\":{\"button\":\"A\"},\"target\":\"/sys/ui\",\"method\":\"back\",\"args\":{},\"cooldown_ms\":250},{\"source\":\"/dev/buttons\",\"event\":\"press\",\"where\":{\"button\":\"C\"},\"target\":\"/sys/ui\",\"method\":\"refresh\",\"args\":{},\"cooldown_ms\":250}]}"},
{"/defaults.json","{}"},
{"/setup.html","<!doctype html><html lang=\"zh\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>Agentic Notes</title><style>*{box-sizing:border-box}body{font:16px system-ui;margin:auto;max-width:900px;padding:24px;background:#faf9f6;color:#20252b}h1{font-size:28px}nav{display:flex;gap:8px;margin:20px 0}button,input,select,textarea{font:inherit;border:1px solid #cdd2d4;border-radius:9px;padding:10px;background:white}button{cursor:pointer}nav button.active,button.primary{background:#286354;color:white}section{display:none;background:white;border:1px solid #e1e2dd;padding:20px;border-radius:14px}section.active{display:block}label{display:block;margin:12px 0}input,textarea{width:100%;margin:6px 0}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px;background:#f3f4f0;padding:12px;border-radius:8px}table{width:100%;border-collapse:collapse}td{padding:8px;border-bottom:1px solid #eee}small{color:#65716c}#message{min-height:24px;margin-top:18px}</style><h1>Agentic Notes</h1><nav><button class=\"active\" data-tab=\"runtime\">系统状态</button><button data-tab=\"files\">文件管理</button><button data-tab=\"settings\">配置</button></nav><section id=\"runtime\" class=\"active\"><button onclick=\"status()\">刷新状态</button><button onclick=\"invoke('/dev/display','drivers').then(show)\">显示驱动</button><button onclick=\"invoke('/sys/ui','refresh').then(show).catch(fail)\">刷新当前页面</button><pre id=\"state\"></pre><label>UI 文件路径<input id=\"uipath\" value=\"/system/ui/home.json\"></label><button onclick=\"invoke('/sys/ui','open',{path:uiPath()}).then(show).catch(fail)\">打开页面</button><button onclick=\"invoke('/sys/ui','back').then(show).catch(fail)\">返回</button><hr><button onclick=\"invoke('/dev/mic','start').then(show).catch(fail)\">开始录音</button><button onclick=\"invoke('/dev/mic','stop').then(show).catch(fail)\">停止录音</button><button onclick=\"invoke('/dev/mic','read').then(show)\">录音状态</button></section><section id=\"files\"><button onclick=\"invoke('/sys/storage','read').then(show)\">存储空间</button><button onclick=\"invoke('/dev/sd','mount').then(()=>list()).catch(fail)\">挂载 SD</button><label>目录<input id=\"dir\" value=\"/storage/user\"></label><button onclick=\"list()\">打开目录</button><input type=\"file\" id=\"upload\"><button onclick=\"uploadFile()\">上传文件</button><table id=\"entries\"></table><button id=\"more\" onclick=\"list(nextCursor)\">下一页</button></section><section id=\"settings\"><label>Wi-Fi SSID<input id=\"ssid\" autocomplete=\"off\"></label><label>Wi-Fi 密码<input id=\"password\" type=\"password\"></label><button class=\"primary\" onclick=\"connect()\">连接</button><button onclick=\"invoke('/net/default','read').then(show)\">网络信息</button><hr><label>服务类型<select id=\"role\"><option value=\"mcp\">MCP</option><option value=\"agent\">上位 Agent</option><option value=\"llm\">LLM API</option></select></label><label>服务地址<input id=\"endpoint\"></label><label>令牌（可留空）<input id=\"endpointToken\"></label><label>模型（可留空）<input id=\"model\"></label><button onclick=\"saveEndpoint()\">保存连接配置</button><button onclick=\"invoke('/sys/connections','read').then(show)\">读取连接配置</button><hr><button onclick=\"loadConfig()\">读取系统策略</button><button onclick=\"invoke('/sys/config','describe').then(show)\">支持的设置</button><label>配置事务<textarea id=\"config\" rows=\"16\">{\"scope\":\"system\",\"changes\":[{\"resource\":\"/dev/mic\",\"settings\":{\"storage\":\"history\"}}]}</textarea></label><label><input id=\"persist\" type=\"checkbox\" style=\"width:auto\">保存配置</label><button onclick=\"applyConfig(true)\">验证</button><button class=\"primary\" onclick=\"applyConfig(false)\">应用配置</button></section><div id=\"message\"></div><pre id=\"result\"></pre><script>\nconst $=id=>document.getElementById(id);let nextCursor=0;document.querySelectorAll('[data-tab]').forEach(b=>b.onclick=()=>{document.querySelectorAll('nav button,section').forEach(x=>x.classList.remove('active'));b.classList.add('active');$(b.dataset.tab).classList.add('active')});\nasync function invoke(path,method='read',args={}){const r=await fetch('/os/invoke',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:crypto.randomUUID?.()||String(Date.now()),path,method,args})});const d=await r.json();if(!d.ok)throw Error(d.error);return d.result}\nfunction show(v){$('result').textContent=JSON.stringify(v,null,2);$('message').textContent=''}function fail(e){$('message').textContent=e.message}function uiPath(){return $('uipath').value}\nasync function status(){try{$('state').textContent=JSON.stringify(await invoke('/sys/stats'),null,2)}catch(e){fail(e)}}\nasync function connect(){try{show(await invoke('/net/default','connect',{ssid:$('ssid').value,password:$('password').value,persist:true}))}catch(e){fail(e)}}\nasync function loadConfig(){try{const c=await invoke('/sys/config');$('config').value=JSON.stringify({scope:'system',expected_revision:c.revision,changes:Object.entries(c.effective).map(([resource,settings])=>({resource,settings}))},null,2);show(c)}catch(e){fail(e)}}\nasync function applyConfig(dry){try{const d=JSON.parse($('config').value);d.persist=$('persist').checked;show(await invoke('/sys/config',dry?'validate':'apply',d))}catch(e){fail(e)}}\nasync function list(cursor=0){try{const base=$('dir').value.replace(/\\/$/,'');const d=await invoke('/fs','list',{file:base,cursor,limit:12});nextCursor=d.next_cursor;$('more').disabled=!d.more;$('entries').replaceChildren();for(const f of d.entries){const tr=document.createElement('tr');const name=document.createElement('td');const btn=document.createElement('button');btn.textContent=f.name;btn.onclick=()=>{if(f.directory){$('dir').value=base+'/'+f.name;list()}else download(base+'/'+f.name)};name.append(btn);const size=document.createElement('td');size.textContent=f.directory?'目录':f.size+' B';tr.append(name,size);$('entries').append(tr)}}catch(e){fail(e)}}\nfunction toHex(b){return Array.from(b,x=>x.toString(16).padStart(2,'0')).join('')}function fromHex(h){return Uint8Array.from(h.match(/../g)||[],s=>parseInt(s,16))}\nasync function uploadFile(){try{const f=$('upload').files[0];if(!f)return;const dest=$('dir').value.replace(/\\/$/,'')+'/'+f.name;for(let at=0;at<f.size||at===0;at+=1024){const b=new Uint8Array(await f.slice(at,at+1024).arrayBuffer());await invoke('/fs','write',{file:dest,offset:String(at),truncate:at===0,hex:toHex(b)});$('message').textContent=`上传 ${Math.min(at+b.length,f.size)} / ${f.size}`;if(!f.size)break}await list();$('message').textContent='文件已保存'}catch(e){fail(e)}}\nasync function download(file){try{let at='0';const chunks=[];for(;;){const d=await invoke('/fs','read',{file,offset:at,length:1024});const b=fromHex(d.hex);if(!b.length)break;chunks.push(b);at=d.next_offset;$('message').textContent='下载 '+at+' B'}const url=URL.createObjectURL(new Blob(chunks));const a=document.createElement('a');a.href=url;a.download=file.split('/').pop();a.click();setTimeout(()=>URL.revokeObjectURL(url),5000);$('message').textContent='下载完成'}catch(e){fail(e)}}\nstatus();\nasync function saveEndpoint(){try{const role=$('role').value;await invoke('/sys/connections','configure',{profiles:{[role]:{url:$('endpoint').value,token:$('endpointToken').value,model:$('model').value,enabled:true}},persist:true});$('message').textContent='配置已保存；连接由所选应用执行'}catch(e){fail(e)}}\n</script></html>"},
{"/startup.lua","-- The factory suite is an ordinary runtime entrypoint. Replacing the configured\n-- startup script replaces its UI and bindings without changing native firmware.\nfunction on_start()\n  local app = notes.call('/sys/ui', 'app_load', {\n    file = notes.resolve('default-app.json')\n  })\n  assert(app.ok, app.error)\n  local shown = notes.call('/sys/ui', 'open', {path = app.result.entry})\n  assert(shown.ok, shown.error)\nend\n"},
{"/ui/home.json","{\"version\":\"0.1.1\",\"assets\":[{\"type\":\"viewScreen\",\"id\":\"home\",\"style\":{\"bgColor\":\"#ffffff\",\"padding\":\"24dp\"},\"layout\":{\"type\":\"flex\",\"flexFlow\":\"column\",\"gap\":\"24dp\"},\"children\":[{\"type\":\"label\",\"id\":\"title\",\"labelProps\":{\"text\":\"Agentic Notes\"},\"style\":{\"fontSize\":\"40sp\",\"textColor\":\"#000000\"}},{\"type\":\"label\",\"id\":\"subtitle\",\"labelProps\":{\"text\":\"Welcome\"},\"style\":{\"fontSize\":\"32sp\",\"textColor\":\"#0000ff\"}},{\"type\":\"label\",\"id\":\"setup\",\"labelProps\":{\"text\":\"Connect to Wi-Fi\\nOpen device settings\"},\"style\":{\"fontSize\":\"24sp\",\"textColor\":\"#000000\"}}]}]}"},
};
inline constexpr size_t ASSET_COUNT=sizeof(ASSETS)/sizeof(ASSETS[0]);
struct Pin {const char*signal;int gpio;int pad;};
inline constexpr Pin PINS[]={
{"KEY_A",0,6},
{"KEY_B",1,7},
{"KEY_C",2,8},
{"BUSY",3,9},
{"BATTERY",4,10},
{"IMU_INT",5,11},
{"SDA",6,12},
{"SCL",7,13},
{"DC",8,14},
{"RESET",9,15},
{"USB_DM",12,16},
{"USB_DP",13,17},
{"SHUTDOWN",14,18},
{"MISO",15,19},
{"SD_CS",16,21},
{"MOSI",17,22},
{"SCLK",18,23},
{"PANEL_CS",19,24},
{"PERIPHERAL_ENABLE",20,25},
{"MIC_WS",21,26},
{"MIC_DATA",22,27},
{"MIC_BCLK",23,28},
};
inline constexpr int DISPLAY_WIDTH=480;
inline constexpr int DISPLAY_HEIGHT=720;
inline constexpr int CRYSTAL_MHZ=40;
inline constexpr float CRYSTAL_LOAD_PF=8;
}
namespace notesos::pins {
inline constexpr int KEY_A=0;
inline constexpr int KEY_B=1;
inline constexpr int KEY_C=2;
inline constexpr int BUSY=3;
inline constexpr int BATTERY=4;
inline constexpr int IMU_INT=5;
inline constexpr int SDA=6;
inline constexpr int SCL=7;
inline constexpr int DC=8;
inline constexpr int RESET=9;
inline constexpr int USB_DM=12;
inline constexpr int USB_DP=13;
inline constexpr int SHUTDOWN=14;
inline constexpr int MISO=15;
inline constexpr int SD_CS=16;
inline constexpr int MOSI=17;
inline constexpr int SCLK=18;
inline constexpr int PANEL_CS=19;
inline constexpr int PERIPHERAL_ENABLE=20;
inline constexpr int MIC_WS=21;
inline constexpr int MIC_DATA=22;
inline constexpr int MIC_BCLK=23;
inline constexpr int buttons[]={KEY_A,KEY_B,KEY_C};
inline constexpr uint32_t I2C_HZ=100000;
inline constexpr float BATTERY_DIVIDER=2.0;
inline constexpr bool VBUS_CLAMPS_BATTERY_ADC=false;
inline constexpr uint32_t CRYSTAL_MHZ=40;
inline constexpr uint32_t CRYSTAL_LOAD_PF=8;
}
