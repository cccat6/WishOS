local C=notes.require('../suite/common.lua')
local function show(hint)
 local w=C.call('/dev/wifi','read') or {};C.open('settings',{ssid=w.ssid or '',address=w.connected and ('http://'..w.ip) or ('http://'..(w.ap_ip or '192.168.4.1')),mac=w.mac or '',agent=C.agent_status(),hint=hint or C.t('浏览器打开后台地址进行详细配置','Open the device address in your browser')})
end
function on_start()
 C.init();show();C.action('ap',function()
  local r,e=C.call('/dev/wifi','configure',{settings={ap_enabled=true},persist=true});show(r and C.t('热点已开启：连接后打开 192.168.4.1','Setup AP enabled: open 192.168.4.1') or e)
 end)
 C.action('language',function() C.preferences.language=C.en() and 'zh' or 'en';C.save_preferences();show() end)
 C.button(function() C.launch('voice',{record=true}) end)
end
