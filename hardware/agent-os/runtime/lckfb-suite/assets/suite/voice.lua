-- Voice workflow is ordinary application behavior. The system broker owns
-- requests and ordered result actions even when a result replaces this VM.
local V={};local ai=notes.require('/system/lib/ai.lua');local cfg,changed,current;local receiver;local generation=0;local taskSessions={}
function V.init(config,notify)
 cfg=config;changed=notify
 receiver=ai.listen(function(action,done,context) if action.topic=='session_control' then changed('session',action.data) elseif action.data and action.data.text then changed('answer',action.data.text,{session=taskSessions[context.id]}) end end)
 notes.on('/dev/mic',{'started','stopped','failed'},function(e) if e.data.recording_id==current then if e.event=='started' then changed('recording',current) else current=nil;changed(e.event=='stopped' and 'processing' or 'failed',e.data.error) end end end)
end
function V.active() return current~=nil end
function V.start(context)
 if current then return end
 local r=notes.call('/dev/mic','start',cfg.recording);if not r.ok then changed('failed',r.error);return end
 current=r.result.recording_id;local id=current;generation=generation+1;local epoch=generation
 local input={kind='voice',recording_id=id};for key,value in pairs(context or {}) do input[key]=value end
 local sent=notes.request('/services/ai','submit',{input=input,timeout_ms=cfg.voice_timeout_ms},function(e)
  local r=notes.call('/services/ai','read',{id=e.data.id});taskSessions[e.data.id]=nil;notes.call('/services/ai','release',{id=e.data.id})
  if e.event=='completed' and epoch==generation then changed('completed') end
  if e.event~='completed' and epoch==generation then if current==id then current=nil;notes.call('/dev/mic','stop',{recording_id=id}) end;changed('failed',r.ok and r.result.error or r.error) end
 end)
 if not sent.ok then notes.call('/dev/mic','stop',{recording_id=id});current=nil;changed('failed',sent.error) else taskSessions[sent.result.id]=context;changed('starting',id) end
end
function V.finish() if current then notes.call('/dev/mic','stop',{recording_id=current}) end end
function V.stop() V.finish();if receiver then receiver.stop() end end
return V
