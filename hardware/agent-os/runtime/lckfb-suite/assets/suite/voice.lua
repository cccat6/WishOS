-- Voice workflow is ordinary application behavior. The system broker owns
-- requests and ordered result actions even when a result replaces this VM.
local V={};local ai=notes.require('/system/lib/ai.lua');local cfg,changed,current;local receiver;local generation=0;local requests={}
function V.init(config,notify)
 cfg=config;changed=notify
 receiver=ai.listen(function(action,done,context) if action.data and action.data.text and (not requests[context.id] or requests[context.id]==generation) then changed('answer',action.data.text) end end)
 notes.on('/dev/mic',{'stopped','failed'},function(e) if e.data.recording_id==current then current=nil;changed(e.event=='stopped' and 'processing' or 'failed',e.data.error) end end)
end
function V.active() return current~=nil end
function V.start()
 if current then return end
 local r=notes.call('/dev/mic','start',cfg.recording);if not r.ok then changed('failed',r.error);return end
 current=r.result.recording_id;local id=current;generation=generation+1;local epoch=generation
 local sent=notes.request('/services/ai','submit',{input={kind='voice',recording_id=id},timeout_ms=cfg.voice_timeout_ms},function(e)
  local r=notes.call('/services/ai','read',{id=e.data.id});notes.call('/services/ai','release',{id=e.data.id})
  if e.event=='completed' and epoch==generation then changed('completed') end
  if e.event~='completed' and epoch==generation then if current==id then current=nil;notes.call('/dev/mic','stop',{recording_id=id}) end;changed('failed',r.ok and r.result.error or r.error) end
 end)
 if not sent.ok then notes.call('/dev/mic','stop',{recording_id=id});current=nil;changed('failed',sent.error) else requests[sent.result.id]=epoch;changed('recording',id) end
end
function V.finish() if current then notes.call('/dev/mic','stop',{recording_id=current}) end end
function V.stop() V.finish();if receiver then receiver.stop() end end
return V
