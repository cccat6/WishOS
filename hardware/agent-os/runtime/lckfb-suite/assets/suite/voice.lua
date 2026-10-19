-- Voice workflow is ordinary application behavior. The system broker owns
-- requests and ordered result actions even when a result replaces this VM.
local V={};local ai=notes.require('/system/lib/ai.lua');local cfg,changed,current;local receiver;local generation=0;local taskSessions={};local progressSequence={};local progressTimer;local latestTask
local function progress(id)
 if not taskSessions[id] or id~=latestTask then return end
 local r=notes.call('/services/ai','read',{id=id});if not r.ok then return end
 local state=r.result;local sequence=state.progress_sequence or 0
 if sequence>(progressSequence[id] or 0) then progressSequence[id]=sequence;changed('progress',state.progress,{session=taskSessions[id]}) end
end
function V.init(config,notify)
 cfg=config;changed=notify
 notes.on('/services/ai','progress',function(e)progress(e.data.id)end)
 local timer=notes.call('/sys/timers','write',{delay_ms=1000,period_ms=1000});if timer.ok then progressTimer=timer.result.id;notes.on('/sys/timers','fired',function(e)if e.data.id==progressTimer and latestTask then progress(latestTask)end end)end
 receiver=ai.listen(function(action,done,context) if action.topic=='session_control' then changed('session',action.data) elseif action.data and action.data.text then return changed('answer',action.data.text,{session=taskSessions[context.id],audio=action.data.audio,audio_error=action.data.audio_error,acknowledge=done}) end end)
 notes.on('/dev/mic',{'started','stopped','failed'},function(e) if e.data.recording_id==current then if e.event=='started' then changed('recording',current) else current=nil;changed(e.event=='stopped' and 'processing' or 'failed',e.data.error) end end end)
end
function V.active() return current~=nil end
function V.start(context)
 if current then return end
 local r=notes.call('/dev/mic','start',cfg.recording);if not r.ok then changed('failed',r.error);return end
 current=r.result.recording_id;local id=current;generation=generation+1;local epoch=generation
 local input={kind='voice',recording_id=id};for key,value in pairs(context or {}) do input[key]=value end
 local sent=notes.request('/services/ai','submit',{input=input,timeout_ms=cfg.voice_timeout_ms},function(e)
  local r=notes.call('/services/ai','read',{id=e.data.id});taskSessions[e.data.id]=nil;progressSequence[e.data.id]=nil;if latestTask==e.data.id then latestTask=nil end;notes.call('/services/ai','release',{id=e.data.id})
  if e.event=='completed' and epoch==generation then changed('completed') end
  if e.event~='completed' and epoch==generation then if current==id then current=nil;notes.call('/dev/mic','stop',{recording_id=id}) end;changed('failed',r.ok and r.result.error or r.error) end
 end)
 if not sent.ok then notes.call('/dev/mic','stop',{recording_id=id});current=nil;changed('failed',sent.error) else latestTask=sent.result.id;taskSessions[sent.result.id]=context or {};changed('starting',id) end
end
function V.finish() if current then notes.call('/dev/mic','stop',{recording_id=current}) end end
function V.cancel()generation=generation+1;local id=current;current=nil;if id then notes.call('/dev/mic','stop',{recording_id=id})end;if latestTask then notes.call('/services/ai','cancel',{id=latestTask})end end
function V.stop() V.finish();if progressTimer then notes.call('/sys/timers','remove',{id=progressTimer});progressTimer=nil end;if receiver then receiver.stop() end end
function V.failure_text(C,code)
 local messages={audio_loss_exceeded={'录音传输丢失过多，请再说一次','Too much audio was lost. Please try again.'},recording_not_found={'录音缓存已过期，请再说一次','Recording expired. Please try again.'},audio_memory_budget={'暂时资源不足，请稍后重试','Resources are busy. Please try again.'},bus_busy={'设备正忙，请稍后重试','Device busy. Please try again.'},capture_stopping={'设备正在准备，请稍后重试','Device is preparing. Please retry shortly.'}}
 local text=messages[code or ''];return text and C.t(text[1],text[2]) or code or ''
end
return V
