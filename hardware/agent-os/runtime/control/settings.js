// This is a browser application flow. The OS supplies owner-scoped expiring
// route sessions and a bounded exit-decision boundary, not a settings page.
function sessionEnded(){settingsClosed=true;settingsSession=null;const text=document.createElement('p');text.setAttribute('role','alert');text.textContent=t('设备已离开设置或连接超时。请重新打开页面。');$('browserSession').replaceChildren(text);document.querySelectorAll('button,input,select,textarea').forEach(e=>e.disabled=true)}
async function sessionPing(){try{return await rawRpc('/net/http-server','session',{action:'ping',id:settingsSession})}catch(e){if(e.code==='session_expired'||e.code==='boot_changed')sessionEnded();throw e}}
async function startSettingsSession(){
 const config=await(await fetch('/control/session.json',{cache:'no-store'})).json();if(!config.application){$('browserSession').hidden=true;return;}
 const state=await rawRpc('/sys/apps');if(state.current!==config.application)await complete('/sys/apps','launch',{id:config.application});
 const r=await rawRpc('/net/http-server','session',{action:'open',path:config.route});settingsSession=r.id;$('browserSession').textContent=t('设备设置已连接');
 const timer=setInterval(()=>{if(settingsClosed)clearInterval(timer);else sessionPing().catch(error)},5000);
 addEventListener('pagehide',()=>{if(settingsSession)navigator.sendBeacon('/os/invoke',new Blob([JSON.stringify({path:'/net/http-server',method:'session',args:{action:'close',id:settingsSession},expected_boot_id:boot})],{type:'application/json'}))},{once:true});
}
startSettingsSession().catch(error);

action('startSharing',async()=>{const p=providers.find(p=>p.id==='user');if(p&&!p.available&&p.prepare)await complete(p.prepare.path,p.prepare.method,p.prepare.args||{});show(await rpc('/services/nfs','start',{provider:'user',export_path:'/sd',lifetime:'system'}));$('shareInfo').textContent=t('共享已开启，可手动关闭。')});
action('stopSharing',async()=>{show(await rpc('/services/nfs','stop'));$('shareInfo').textContent=t('共享已关闭')});
