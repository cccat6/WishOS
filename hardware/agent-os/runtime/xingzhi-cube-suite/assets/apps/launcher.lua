local C=notes.require('../suite/common.lua');local help=notes.require('../suite/help-content.lua');local helpPage=1;local index=1;local mode='menu';local apps={};local items;local appCursor=0;local appNext;local appPrevious={};local choices={}
local function hint()return C.t('单击切换 · 双击确认\n三击回桌面 · 长按说话','Click: select · Double: open\nTriple: Home · Hold: talk')end
local function show()
 local reuse=mode=='menu'and C.view;mode='menu';local s=C.status();items={C.t('语音对话','Talk to Agent'),C.t('已安装应用','Installed apps'),C.t('网络与设置','Network and settings'),C.t('使用帮助','Help'),C.t('语言 / Language','Language / 语言')}
 s.title='WishOS';s.message=items[index];s.detail='';local preview=C.choice_preview(items,index);s.previous_choice=preview.previous_choice;s.next_choice=preview.next_choice;s.item_icon=notes.resolve('../icons/'..({'voice','apps','settings','device','settings'})[index]..'.png');s.hint=hint();if reuse then C.update(s)else C.open(s)end
end
local function network_values(w)return {message='http://'..(w.connected and w.ip or w.ap_ip or '192.168.4.1')..'/',ssid=w.connected and(w.ssid or '')or(w.ap_ssid or 'WishOS'),mac=w.mac or ''}end
local function setup()
 mode='setup';local w=C.call('/dev/wifi')or {};if not w.connected and not w.ap_active then C.call('/dev/wifi','configure',{settings={enabled=true,ap_enabled=true}});w=C.call('/dev/wifi')or {}end
 local values=network_values(w);values.title=C.t('网络与设置','Network and settings');values.hint=C.t('双击返回','Double: Back');C.open(values,'network')
end
local function welcome()
 local reuse=mode=='welcome'and C.view;mode='welcome';local w=C.call('/dev/wifi')or {};local v={title='WishOS',message=C.t('欢迎使用 · 中文','Welcome · English'),detail=C.t('单击切换语言；双击配网','Single: language; double: setup')..'\n'..(w.mac or ''),hint=hint()};if reuse then C.update(v)else C.open(v,'welcome')end
end
local function app_list(newPage)
 local reuse=mode=='apps'and C.view and not newPage;mode='apps';local r=C.call('/sys/apps','list',{cursor=appCursor,limit=4})or {};apps={};choices={}
 for _,v in ipairs(r.applications or {})do if v.id~='launcher'then apps[#apps+1]=v;choices[#choices+1]={title=v.title or v.id,id=v.id}end end
 appNext=r.more and r.next_cursor or nil
 if appNext then choices[#choices+1]={title=C.t('下一页','Next page'),action='next'}end
 if #appPrevious>0 then choices[#choices+1]={title=C.t('上一页','Previous page'),action='previous'}end
 choices[#choices+1]={title=C.t('返回桌面','Back to desktop'),action='back'};index=math.min(index,#choices)
 local names={};for _,choice in ipairs(choices)do names[#names+1]=choice.title end;local preview=C.choice_preview(names,index);local v={previous_choice=preview.previous_choice,next_choice=preview.next_choice,item_icon=notes.resolve('../icons/apps.png'),title=C.t('已安装应用','Installed apps'),message=choices[index].title,detail='',hint=hint()};if reuse then C.update(v)else C.open(v)end
end
local function help_view()
 mode='help';local r=help[helpPage];C.open({title=C.t(r[1],r[2]),message=C.t(r[3],r[4]),page=tostring(helpPage)..' / '..#help,hint=C.t('单击翻页 · 双击返回','Click next / Double back')},C.preferences.language=='en'and 'help.en'or 'help')
end
local function next_item()
 if mode=='help'then helpPage=helpPage%#help+1;help_view()
 elseif mode=='welcome'then C.preferences.language=C.preferences.language=='en'and 'zh'or 'en';welcome()
 elseif mode=='apps'then index=index%#choices+1;app_list()
 elseif mode=='menu'then index=index%#items+1;show()
 else C.update({hint=C.t('双击返回','Double: Back')})end
end
local function confirm()
 if mode=='welcome'then local saved=C.change_preferences({onboarded=true});setup();if not saved then C.update({hint=C.t('设置未保存；仍可配网','Not saved; setup is available')})end;return end
 if mode=='setup'or mode=='help'then index=1;show();return end
 if mode=='apps'then local item=choices[index];if item.id then C.call('/sys/apps','launch',{id=item.id})elseif item.action=='next'then appPrevious[#appPrevious+1]=appCursor;appCursor=appNext;index=1;app_list(true)elseif item.action=='previous'then appCursor=table.remove(appPrevious);index=1;app_list(true)else index=1;show()end;return end
 if index==1 then C.call('/sys/apps','launch',{id='voice'})
 elseif index==2 then index=1;appCursor=0;appPrevious={};app_list()
 elseif index==3 then setup()
 elseif index==4 then helpPage=1;help_view()
 else C.change_preferences({language=C.preferences.language=='en'and 'zh'or 'en'});show()end
end
function on_start()
 C.init();local function enter()if C.preferences.onboarded then show()else welcome()end end
 local display=C.call('/dev/display')or {}
 if display.content_valid==false then
  local waiting=true;notes.on('/dev/display',{'completed','failed'},function(e)if waiting and e.data.view_id==C.view then waiting=false;if e.event=='failed'then enter()else C.timer(C.config.boot_logo_ms,0,enter)end end end)
  mode='splash';C.open({},'splash')
 else enter()end
 C.timer(1000,1000,function()C.update(C.status())end)
 notes.on('/dev/wifi',{'connected','disconnected','configuration_applied'},function()if mode=='setup'then C.update(network_values(C.call('/dev/wifi')or {}))end end)
 notes.on('/dev/buttons','click',function(e)if mode~='splash'then C.click(e.data,next_item,confirm)end end)
end
