-- Replaceable push-to-talk behavior. The OS provides capture, files and remote
-- operations; this application chooses when to invoke them and what to display.
local V={}
local ai=notes.require("/system/lib/ai.lua")
local receiver
local cfg,changed,held,current,retry=nil,nil,false,nil,nil
local generation=0
local function call(p,m,a) return notes.call(p,m,a or {}) end
local function finish(id)
  if current==id then current=nil;changed('processing') end
end
local function start()
  if not held or current then return end
  local r=call('/dev/mic','start',cfg.recording or {})
  if not r.ok then
    if r.error=='mic_busy' or r.error=='capture_stopping' or r.error=='storage_preparing' then
      local t=call('/sys/timers','write',{delay_ms=100})
      if t.ok then retry=t.result.id end
    else held=false;changed('failed',r.error) end
    return
  end
  current=r.result.recording_id
  local id=current;generation=generation+1;local epoch=generation
  local a={input={kind='voice',recording_id=id,file=r.result.file},timeout_ms=cfg.voice_timeout_ms or 900000}
  local sent=notes.request('/services/ai','submit',a,function(e)
    local result=call('/services/ai','read',{id=e.data.id})
    call('/services/ai','release',{id=e.data.id})
    if e.event~='completed' and epoch==generation then changed('failed',result.ok and result.result.error or result.error) end
  end)
  if not sent.ok then call('/dev/mic','stop',{recording_id=id});current=nil;changed('failed',sent.error)
  else changed('recording',id) end
end
function V.init(c,notify)
  cfg=c;changed=notify
  receiver=ai.listen(function(action)
    if action.data and action.data.text then changed('answer',action.data.text) end
  end)
  notes.on('/sys/timers',nil,function(e) if e.data.id==retry then retry=nil;start() end end)
  notes.on('/dev/mic',nil,function(e)
    if e.data.recording_id==current then
      if e.event=='stopped' then finish(current)
      elseif e.event=='failed' then current=nil;changed('failed',e.data.error) end
    end
  end)
end
function V.press() if cfg.voice_enabled==false then changed('unconfigured');return end held=true;start() end
function V.release()
  held=false;if retry then call('/sys/timers','remove',{id=retry});retry=nil end
  if current then local id=current;local r=call('/dev/mic','stop',{recording_id=id});if not r.ok then changed('failed',r.error) end end
end
function V.stop() V.release();if receiver then receiver.stop();receiver=nil end end
return V
