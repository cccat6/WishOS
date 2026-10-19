local M={}
local locale=notes.require('locale.lua')
local t=locale.text
local appMenu=notes.require("apps.lua")
local uiRoot=notes.resolve("../ui")
local manifest=notes.resolve("../default-app.json")
local cfg,page,view,selection,timer,rotation='','welcome',nil,1,nil,0
local sleepRevision=nil
local message,signature='',''
local function call(path,method,args)
  local r=notes.call(path,method,args or {})
  if not r.ok then message=r.error or t('操作失败',"Operation failed") end
  return r
end
local function read(path)
  local r=notes.call(path,'read',{})
  return r.ok and r.result or {}
end
local function clip(value,n)
  local s=tostring(value or '')
  if utf8.len(s) and utf8.len(s)>n then return string.sub(s,1,utf8.offset(s,n+1)-1)..'…' end
  return s
end
local function values()
  local w=read('/dev/wifi')
  local b=read('/dev/battery')
  local v={ssid=clip(w.connected and w.ssid or t('未连接',"Disconnected"),24),ip=w.connected and w.ip or (w.ap_active and w.ap_ip or t('未启用热点',"No hotspot")),mac=w.mac or '',message=clip(message,40),network=w.connected and t('已联网',"Online") or t('未联网',"Offline")}
  v.power=b.external_power==true and t('外部供电',"External power") or b.external_power==false and t('电池供电',"Battery power") or t('正在读取电源',"Reading power")
  v.voltage=type(b.mv)=='number' and string.format('%.2f V',b.mv/1000) or t('等待电压读数',"Waiting for voltage")
  v.ap=w.ap_ssid or ('WishOS-'..string.sub(v.mac,-8))
  if page=='menu' then for i,item in ipairs(cfg.menu) do v['item'..i]=t(item.label,item.label_en) end end
  if page=='services' then
    local link=read('/services/link');local applied=link.applied or {}
    v.connection=link.connected and t('已连接',"Connected") or (link.error~='' and link.error or t('等待上位服务',"Waiting for service"))
    v.endpoint=clip(applied.host or t('未配置',"Not configured"),40)
    v.route=clip((read('/services/ai').configured or {}).route or t('未配置',"Not configured"),40)
  elseif page=='status' then
    local stats=read('/sys/stats');v.memory=string.format('%.1f KiB', (stats.heap_free_bytes or 0)/1024)
    local sd=read('/dev/sd');v.storage=sd.mounted and t('已挂载',"Mounted") or t('未挂载',"Not mounted")
  elseif page=='apps' then for k,value in pairs(appMenu.values()) do v[k]=value end
  elseif page=='storage' then
    local r=notes.call('/fs','list',{file='/storage/user',limit=5,cursor=0})
    local rows={}
    if r.ok then for _,f in ipairs(r.result.entries or {}) do rows[#rows+1]=(f.directory and t('[目录] ',"[Dir] ") or '')..clip(f.name,22) end
    else rows[1]=t('存储卡尚未就绪',"Storage is not ready");rows[2]=t('按 B 尝试挂载',"Press B to mount");v.message=r.error or '' end
    v.files=table.concat(rows,'\n');if #rows==0 then v.files=t('此目录没有内容',"Directory is empty") end

  end
  return locale.values(page,v)
end
local function present(activate,force)
  local mode=(cfg.pages[page] or cfg.pages.welcome).driver
  local path=uiRoot..'/'..((cfg.pages[page] or {}).layout or page)..'.json'
  local v=values();local key=page..mode..rotation..selection..notes.encode(v)
  if not activate and not force and key==signature then return end
  local args={path=path,driver=mode,rotation=rotation,values=v,force=force or false,focus=page=='menu' and 'row'..selection or ''}
  local r
  if view then args.view_id=view;r=call('/sys/ui','update',args)
  else args.history='replace';r=call('/sys/ui','open',args) end
  if r.ok then
    view=r.result.view_id;signature=key
    if page=='sleep' then sleepRevision=r.result.revision end
    if activate then call('/sys/ui','activate',{view_id=view,history='replace'}) end
  end
end
local function go(name,activate)
  page=name;message='';signature='';present(activate,false)
end
local function startTimer()
  local r=call('/sys/timers','write',{delay_ms=cfg.status_interval_ms,period_ms=cfg.status_interval_ms})
  if r.ok then timer=r.result.id end
end
function M.prepare(c)
  assert(type(c.pages)=='table' and type(c.menu)=='table')
  for name,spec in pairs(c.pages) do
    local r=notes.call('/fs','stat',{file=uiRoot..'/'..(spec.layout or name)..'.json'})
    assert(r.ok,'缺少页面 '..name)
  end
end
function M.start(c)
  cfg=c
  local loaded=call('/sys/ui','app_load',{file=manifest});assert(loaded.ok,loaded.error)
  for _,source in ipairs({'/dev/buttons','/dev/battery','/dev/wifi','/dev/display','/dev/imu','/sys/timers','/sys/power'}) do notes.on(source,nil,M.event) end
  call('/dev/buttons','configure',{defaults={max_clicks=1}})
  locale.restore(function() local w=read('/dev/wifi');page=locale.chosen and (w.connected and 'home' or 'welcome') or 'language';present(true,false);startTimer() end)
end
local function select()
  if page=='welcome' then call('/dev/wifi','resume_auto');go('network',true)
  elseif page=='menu' then go(cfg.menu[selection].page,true)
  elseif page=='network' then call('/dev/wifi','resume_auto');present(false,true)
  elseif page=='apps' then local r=appMenu.open();if r and not r.ok then message=r.error;present(false,false) end
  elseif page=='storage' then
    local r=notes.request('/dev/sd','mount',{},function(e) if e.event=='operation_failed' then message=e.data.error end present(false,true) end)
    if not r.ok then message=r.error;present(false,true) end
  else go('menu',true) end
end
function M.event(e)
  local d=e.data or {}
  if e.source=='/dev/buttons' then
    local key=d.button
    if page=='language' and e.event=='click' then
      if key==cfg.buttons.previous or key==cfg.buttons.select then local r=locale.select(key==cfg.buttons.select and 'en' or 'zh');go('welcome',true);if not r.ok then message=r.error;present(false,false) end end
      return true
    end
    if page=='services' and e.event=='click' and key==cfg.buttons.next then go('language',true);return true end
    if e.event=='long_press' and key==cfg.buttons.previous then go('menu',true)
    elseif e.event=='long_press' and key==cfg.buttons.select then call('/sys/apps','voice')
    elseif e.event=='release' and key==cfg.buttons.select then return false
    elseif e.event=='click' then
      if page=='welcome' and key==cfg.buttons.previous then call('/dev/wifi','configure',{settings={ap_enabled=true}});go('network',true)
      elseif page=='welcome' and key==cfg.buttons.next then go('menu',true)
      elseif page=='apps' and (key==cfg.buttons.previous or key==cfg.buttons.next) then appMenu.move(key==cfg.buttons.next and 1 or -1);present(false,false)
      elseif page=='menu' and (key==cfg.buttons.previous or key==cfg.buttons.next) then selection=(selection-1+(key==cfg.buttons.next and 1 or #cfg.menu-1))%#cfg.menu+1;present(false,false)
      elseif key==cfg.buttons.select then select()
      else go('menu',true) end
    else return false end
    return cfg.consume_buttons
  elseif e.source=='/sys/timers' and d.id==timer then present(false,false)
  elseif e.source=='/dev/wifi' and e.event=='changed' then
    if d.connected and page=='welcome' then go('network',false) else present(false,false) end
  elseif e.source=='/dev/battery' and e.event=='changed' then present(false,false)
  elseif e.source=='/dev/imu' and cfg.auto_rotate and type(d.rotation)=='number' then rotation=d.rotation;present(false,false)
  elseif e.source=='/dev/display' and e.event=='completed' and page=='sleep' and sleepRevision and d.view_id==view and d.view_revision==sleepRevision then
    if (read('/sys/ui').presentation or {}).active_view~=view then sleepRevision=nil;return false end
    sleepRevision=nil;if timer then call('/sys/timers','remove',{id=timer});timer=nil end
    local r=call('/sys/wake','write',{buttons_mask=7,timer_ms=cfg.sleep_timer_ms,imu=false})
    if r.ok then r=call('/sys/power','sleep',{mode='deep'}) end
    if not r.ok then go('status',false);message=r.error;present(false,false);startTimer() end
  elseif e.source=='/sys/power' and e.event=='aborted' then go('status',false);message=t('休眠请求未执行',"Sleep did not start");present(false,false);if not timer then startTimer() end
  end
  return false
end
return M
