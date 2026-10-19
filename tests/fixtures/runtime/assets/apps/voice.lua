local locale=notes.require('../suite/locale.lua')
local t=locale.text
local voice=notes.require('../suite/voice.lua')
local config=notes.require('../suite/config.lua')
local view,timer
local function show(capture,message)
 local args={path=notes.resolve('../ui/recording.json'),driver=config.pages.recording.driver,values=locale.values('recording',{capture=capture,help=t('松开 B 结束；长按 A 返回桌面','Release B to send; hold A for Home'),message=message or ''})}
 if view then args.view_id=view end
 local result=notes.call('/sys/ui',view and 'update' or 'open',args)
 if result.ok then view=result.result.view_id;notes.call('/sys/ui','activate',{view_id=view,history='replace'}) end
end
local function start()
 voice.init(config,function(state,detail)
  if state=='recording' then if config.show_recording then show(t('正在录音','Recording')) end
  elseif state=='processing' then if config.show_processing then show(t('正在处理','Processing')) end
  elseif state=='answer' then show(t('回复','Reply'),detail)
  elseif state=='failed' then show(t('请求未完成','Request failed'),detail)
  elseif state=='unconfigured' then show(t('请先连接上位服务','Connect to an upstream service first')) end
 end)
 notes.on('/sys/apps','voice_requested',function() local n=notes.call('/sys/navigation','read');if n.ok and n.result.voice_held then voice.press() end end)
 notes.on('/sys/navigation','voice_released',function() voice.release() end)
 notes.on('/dev/buttons','press',function(e) if e.data.button==config.buttons.select then voice.press() end end)
 notes.on('/dev/buttons','release',function(e) if e.data.button==config.buttons.select then voice.release() end end)
 local navigation=notes.call('/sys/navigation','read')
 if navigation.ok and navigation.result.voice_held then voice.press() else show(t('按住 B 说话','Hold B to speak')) end
end
function on_start() locale.restore(start) end
function on_stop() voice.stop() end
