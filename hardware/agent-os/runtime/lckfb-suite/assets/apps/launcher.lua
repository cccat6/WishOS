-- Ordinary launcher application, using the same services as installed apps.
local C=notes.require('../suite/common.lua');local rows={};local page=0;local starts={[0]=0}
local function clock_text() local t=C.call('/services/time','read') or {};return t.wall_clock_valid and t.local_time:sub(12,16) or '' end
local function update_clock()
 local ui=C.call('/sys/ui');if not ui or ui.view_id~=C.view or ui.path~=C.path('home') then return end
 local text=clock_text();if text~=C.model.clock then C.update({clock=text}) end
end
local function desktop() C.open('home',{connection=C.agent_status(),clock=clock_text()});C.action('voice',function() C.launch('voice') end);C.action('apps',function() page=0;starts={[0]=0};C.show_apps() end);C.action('device',function() C.launch('device') end);C.action('settings',function() C.launch('settings') end) end
function C.show_apps()
 rows={};local cursor=starts[page] or 0;local more=false;local own=C.call('/sys/apps','read') or {}
 repeat local r=C.call('/sys/apps','list',{cursor=cursor,limit=3-#rows});if not r then break end;for _,a in ipairs(r.applications) do if a.id~=own.current then rows[#rows+1]=a end end;cursor=r.next_cursor;more=r.more until #rows==3 or not more
 starts[page+1]=cursor
 local v={};for i=0,2 do local a=rows[i+1];v['app'..i..'_title']=a and a.title or C.t('暂无应用','No applications');v['app'..i..'_icon']=notes.resolve('../icons/'..(a and (a.id=='voice' or a.id=='camera' or a.id=='device' or a.id=='settings' or a.id=='files') and a.id or 'apps')..'.png') end
 C.open('apps',v);for i=0,2 do C.action('app'..i,function() local a=rows[i+1];if a then C.launch(a.id) end end) end
 C.action('next',function() if more then page=page+1;C.show_apps() end end);C.action('previous',function() if page>0 then page=page-1;C.show_apps() else desktop() end end)
end
local function welcome() C.open('welcome');C.action('ready',function() C.preferences.onboarded=true;C.save_preferences();desktop() end) end
local function choose(lang) C.preferences.language=lang;C.save_preferences();welcome() end
function on_start()
 C.init();C.button(function() C.launch('voice',{record=true}) end)
 C.timer(C.config.clock_poll_interval_ms,C.config.clock_poll_interval_ms,update_clock)
 notes.on('/services/time','synchronized',update_clock)
 notes.on('/dev/wifi',{'connected','disconnected'},function() local r=C.call('/sys/ui');if r and r.path==C.path('home') then C.update({connection=C.agent_status()}) end end)
 notes.on('/services/link',{'connected','disconnected'},function() local r=C.call('/sys/ui');if r and r.path==C.path('home') then C.update({connection=C.agent_status()}) end end)
 C.action('home',desktop)
 if not C.preferences.language then C.preferences.language='zh' end
 if not C.preferences.onboarded then C.open('language');C.action('zh',function() choose('zh') end);C.action('en',function() choose('en') end) else desktop() end
 C.prepare_sd()
 C.call('/net/http-server','route',{path='/',content_type='text/html; charset=utf-8',body='<meta charset="utf-8"><meta http-equiv="refresh" content="0;url=/control/setup.html"><a href="/control/setup.html">WishOS</a>'})
end
