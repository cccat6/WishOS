local C={};C.config=notes.require('../suite/config.lua');local actions={};local down=true;local pressed;local touchPending=false;local touchTimer;local touchStreamMethod;local timers={};local preferences=notes.resolve('../preferences.json');local view
function C.call(path,method,args) local r=notes.call(path,method,args or {});if r.ok then return r.result end return nil,r.error end
function C.unhex(s) local bytes={};for i=1,#s,2 do bytes[#bytes+1]=string.char(tonumber(s:sub(i,i+1),16)) end;return table.concat(bytes) end
function C.read_json(file) local r=C.call('/fs','read',{file=file,length=1024});if r then local ok,data=pcall(notes.decode,C.unhex(r.hex));if ok then return data end end end
C.preferences={language='zh',onboarded=false}
function C.en() return C.preferences.language=='en' end
function C.t(zh,en) if C.en() then return en end return zh end
function C.change_preferences(values,removed)
 local next={};for key,value in pairs(C.preferences) do next[key]=value end;for key,value in pairs(values or {}) do next[key]=value end;for _,key in ipairs(removed or {}) do next[key]=nil end
 local r,e=C.call('/fs','write',{file=preferences,text=notes.encode(next),truncate=true});if r then C.preferences=next end;return r,e
end
function C.path(name) return notes.resolve('../ui/'..name..(C.en() and '.en' or '')..'.json') end
function C.open(name,values,hidden)
 local r,e=C.call('/sys/ui','open',{path=C.path(name),history='replace',values=values or {},hidden=notes.array(hidden or {})})
 assert(r,e);view=r.view_id;C.view=view;C.layoutRevision=r.revision;C.model=values or {};pressed=nil;return r
end
function C.update(values) if not view then return end;local next={};for k,v in pairs(C.model) do next[k]=v end;for k,v in pairs(values) do next[k]=v end;local r,e=C.call('/sys/ui','update',{view_id=view,values=next});if r then C.model=next end;return r,e end
function C.action(id,fn) actions[id]=fn end
function C.home() C.call('/sys/apps','home') end
function C.launch(id,arguments) local r,e=C.call('/sys/apps','launch',{id=id,arguments=arguments or {}});if not r then C.update({status=e or '',message=e or ''}) end end
function C.timer(delay,period,fn)
 local r,e=C.call('/sys/timers','write',{delay_ms=delay,period_ms=period or 0});if r then timers[r.id]={fn=fn,once=not period or period==0};return r.id end return nil,e
end
function C.cancel(id) if id then timers[id]=nil;C.call('/sys/timers','remove',{id=id}) end end
function C.button(fn) notes.on('/dev/buttons','release',function(e) local ui=C.call('/sys/ui');if ui and e.data.presentation_id==ui.presentation.active_view then fn(e) end end) end
function C.agent_status()
 local r=C.call('/services/link','read');return r and r.connected and C.t('Agent 已连接','Agent connected') or C.t('未连接 Agent','Agent not connected')
end
function C.beep()
 local r,e=C.call('/dev/speaker','start',{sample_rate_hz=16000,channels=1,encoding='ima-adpcm-block',buffer_blocks=4});if not r then return false,e end
 for i=0,3 do
  local block,err=C.call('/fs','read',{file=notes.resolve('../brand/ready.adpcm'),offset=tostring(i*331),length=331})
  if not block then C.call('/dev/speaker','stop',{playback_id=r.playback_id});return false,err end
  local sent,err=C.call('/dev/speaker','write',{playback_id=r.playback_id,sequence=i,hex=block.hex});if not sent then C.call('/dev/speaker','stop',{playback_id=r.playback_id});return false,err end
 end
 local done,err=C.call('/dev/speaker','finish',{playback_id=r.playback_id});return done~=nil,err
end
function C.prepare_sd(done)
 local finished=false;local timer;local operation;local clock=C.call('/sys/clock') or {};local deadline=(clock.monotonic_ms or 0)+C.config.storage_wait_timeout_ms
 local function finish(state,error) if finished then return end;finished=true;C.cancel(timer);if done then done(state,error) end end
 local function check()
  if finished then return end
  local now=C.call('/sys/clock') or {};if (now.monotonic_ms or 0)>=deadline then if operation then C.call('/dev/sd','cancel',{operation_id=operation}) end;finish(nil,'storage_preparation_timeout');return end
  local state,error=C.call('/dev/sd');if not state then finish(nil,error);return end
  if state.operation_pending then return end
  if state.mounted then finish(state);return end
  local request=notes.request('/dev/sd','mount',{},function(event)
   operation=nil;if finished then return end
   if event.event=='operation_completed' or event.data.error=='cancelled' then check() else finish(nil,event.data.error) end
  end)
  if request.ok then operation=request.result.operation_id elseif request.error~='operation_active' then finish(nil,request.error) end
 end
 timer=C.timer(C.config.storage_retry_ms,C.config.storage_retry_ms,check);check()
end
function C.init()
 C.preferences=C.read_json(preferences) or C.preferences
 actions.home=C.home
 local cfg=C.call('/sys/config','read',{scope='system'});local ui=cfg and cfg.effective and cfg.effective['/sys/ui'] or {}
 if not ui.font_file or ui.font_file=='' then
  C.call('/sys/config','write',{scope='system',persist=true,changes={{resource='/sys/ui',settings={font_file=notes.resolve('../fonts/NotesSansSC-Regular.ttf')}}}})
 end
 notes.on('/sys/timers','fired',function(e) local callback=timers[e.data.id];if callback then if callback.once then timers[e.data.id]=nil end;callback.fn(e) end end)
 local function touch_event(e)
  touchPending=false
  if e.data.presentation_id and e.data.presentation_id~=view then pressed=nil;down=e.data.count~=0;return end
  if e.data.count==1 then
   local p=e.data.contacts[1];local hit=C.call('/sys/ui','hit_test',{view_id=view,x=p.x,y=p.y});local id
   if hit and hit.revision>=C.layoutRevision then for _,candidate in ipairs(hit.ids) do if actions[candidate] then id=candidate;break end end end
   if not down and id then pressed={id=id,view=view,layout=C.layoutRevision} elseif pressed and pressed.id~=id then pressed=nil end
   down=true
  elseif e.data.count>1 then down=true;pressed=nil
  else
   local target=pressed;pressed=nil;down=false
   if target and target.view==view and target.layout==C.layoutRevision then actions[target.id](e) end
  end
 end
 local function poll_touch()
  if touchTimer then return end
  notes.on('/dev/touch','completed',touch_event)
  touchTimer=C.timer(C.config.touch_interval_ms,C.config.touch_interval_ms,function() if not touchPending and view then local r=C.call('/dev/touch','measure');touchPending=r~=nil end end)
 end
 notes.on('/dev/touch','failed',function(e) touchPending=false;pressed=nil;down=true;if e.data.input_stream and touchStreamMethod then local stopped=C.call('/dev/touch',touchStreamMethod,{enabled=false});if stopped then poll_touch() end end end)
 notes.on('/dev/touch','input_overflow',function() pressed=nil;down=true end)
 local capability=C.call('/sys/capabilities','describe',{path='/dev/touch'}) or {};local stream=capability.input_stream
 if stream then touchStreamMethod=stream.method;notes.on('/dev/touch',stream.event,touch_event);local state=C.call('/dev/touch') or {};local request={enabled=true};if not state.input_stream or not state.input_stream.enabled or state.input_stream.owner~=0 then request.interval_ms=C.config.touch_capture_interval_ms end;local active=C.call('/dev/touch',stream.method,request);if not active then poll_touch() end else poll_touch() end
end
return C
