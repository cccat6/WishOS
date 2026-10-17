local C=notes.require('../suite/common.lua')
local screen='settings';local pending;local guarded=false;local clients=0
local function show(name,hint)
 screen=name or screen
 local w=C.call('/dev/wifi','read') or {};local ip=w.connected and w.ip or w.ap_ip or '192.168.4.1'
 if screen=='settings' then C.open(screen,{ssid=w.ssid or C.t('尚未联网','Not connected'),address=ip,mac='MAC '..(w.mac or ''),qr='http://'..ip..'/control/setup.html',clients=clients>0 and C.t('手机已连接 · ','Browser connected · ')..clients or C.t('等待手机连接','Waiting for browser')})
 elseif screen=='sharing' then local s=C.call('/services/nfs','read') or {};C.open(screen,{share_status=(s.running and C.t('已开启','Enabled') or C.t('未开启','Disabled'))..'\n'..ip..':/sd',share_toggle=s.running and C.t('关闭共享','Stop sharing') or C.t('开启共享','Start sharing')})
 else C.open(screen,{hint=hint or ''}) end
end
local function refresh()
 local web=C.call('/net/http-server','read') or {};clients=web.clients or 0
 local enabled=clients>0;if enabled~=guarded then local r=C.call('/sys/apps','guard_exit',{enabled=enabled,timeout_ms=30000});guarded=r~=nil and enabled end
 if screen=='sharing' then local share=C.call('/services/nfs','read') or {};local title=share.running and C.t('关闭共享','Stop sharing') or C.t('开启共享','Start sharing');if C.model.share_toggle~=title then show('sharing') end end
 if screen=='settings' then local label=clients>0 and C.t('手机已连接 · ','Browser connected · ')..clients or C.t('等待手机连接','Waiting for browser');if C.model.clients~=label then C.update({clients=label}) end end
end
local function back()
 if pending then C.call('/sys/apps','resolve_exit',{operation_id=pending,allow=false});pending=nil;show('settings');return end
 if screen=='settings' then C.home() elseif screen=='sharing' then show('settings-options') else show('settings') end
end
function on_start()
 C.init()
 C.call('/net/http-server','route',{path='/app/settings',body='WishOS settings',content_type='text/plain'})
 show('settings');refresh();C.timer(1000,1000,refresh)
 notes.on('/net/http-server','session_changed',refresh)
 notes.on('/dev/wifi',{'connected','disconnected'},function() if not pending then show(screen) end end)
 notes.on('/sys/apps','exit_requested',function(e) pending=e.data.operation_id;show('confirm-exit') end)
 notes.on('/sys/apps','exit_cancelled',function(e) if pending==e.data.operation_id then pending=nil;show('settings') end end)
 C.action('home',back);C.action('options',function() show('settings-options') end)
 C.action('stay',function() if pending then C.call('/sys/apps','resolve_exit',{operation_id=pending,allow=false});pending=nil;show('settings') end end)
 C.action('leave',function() if pending then C.call('/sys/apps','resolve_exit',{operation_id=pending,allow=true});pending=nil end end)
 C.action('language',function() local saved,e=C.change_preferences({language=C.en() and 'zh' or 'en'});show('settings-options',saved and '' or e) end)
 C.action('ap',function() local r,e=C.call('/dev/wifi','configure',{settings={ap_enabled=true},persist=true});show('settings-options',r and C.t('热点已开启','Setup AP enabled') or e) end)
 C.action('sharing',function() show('sharing') end)
 C.action('toggle',function()
  local s=C.call('/services/nfs','read') or {}
  if s.running then local r,e=C.call('/services/nfs','stop');if r then show('sharing') else C.update({share_status=e or ''}) end;return end
  C.prepare_sd(function(card,e) if not card or not card.mounted then C.update({share_status=e or C.t('SD 卡不可用','SD card unavailable')});return end
   local r,err=C.call('/services/nfs','start',{provider='user',export_path='/sd',lifetime='system'});if r then show('sharing') else C.update({share_status=err or ''}) end
  end)
 end)

 C.button(function() if not pending then C.launch('voice',{record=true}) end end)
end
