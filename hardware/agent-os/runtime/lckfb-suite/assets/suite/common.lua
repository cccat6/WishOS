local C={};C.config=notes.require('../suite/config.lua');local actions={};local down=false;local touchPending=false;local timers={};local preferences=notes.resolve('../preferences.json');local view
function C.call(path,method,args) local r=notes.call(path,method,args or {});if r.ok then return r.result end return nil,r.error end
function C.unhex(s) local bytes={};for i=1,#s,2 do bytes[#bytes+1]=string.char(tonumber(s:sub(i,i+1),16)) end;return table.concat(bytes) end
function C.read_json(file) local r=C.call('/fs','read',{file=file,length=1024});if r then local ok,data=pcall(notes.decode,C.unhex(r.hex));if ok then return data end end end
C.preferences={language='zh',onboarded=false}
function C.en() return C.preferences.language=='en' end
function C.t(zh,en) if C.en() then return en end return zh end
function C.save_preferences() return C.call('/fs','write',{file=preferences,text=notes.encode(C.preferences),truncate=true}) end
function C.path(name) return notes.resolve('../ui/'..name..(C.en() and '.en' or '')..'.json') end
function C.open(name,values)
 local r,e=C.call('/sys/ui','open',{path=C.path(name),history='replace',values=values or {}})
 assert(r,e);view=r.view_id;C.view=view;C.layoutRevision=r.revision;C.model=values or {};return r
end
function C.update(values) if not view then return end;local next={};for k,v in pairs(C.model) do next[k]=v end;for k,v in pairs(values) do next[k]=v end;local r,e=C.call('/sys/ui','update',{view_id=view,values=next});if r then C.model=next end;return r,e end
function C.action(id,fn) actions[id]=fn end
function C.home() C.call('/sys/apps','home') end
function C.launch(id,arguments) local r,e=C.call('/sys/apps','launch',{id=id,arguments=arguments or {}});if not r then C.update({status=e or '',message=e or ''}) end end
function C.timer(delay,period,fn)
 local r,e=C.call('/sys/timers','write',{delay_ms=delay,period_ms=period or 0});if r then timers[r.id]=fn;return r.id end return nil,e
end
function C.cancel(id) if id then timers[id]=nil;C.call('/sys/timers','remove',{id=id}) end end
function C.button(fn) notes.on('/dev/buttons','click',function(e) if e.data.count==1 then fn(e) end end) end
function C.agent_status()
 local r=C.call('/services/link','read');return r and r.connected and C.t('Agent 已连接','Agent connected') or C.t('未连接 Agent','Agent not connected')
end
function C.beep()
 local r=C.call('/dev/speaker','start',{sample_rate_hz=16000,channels=1,encoding='ima-adpcm-block',buffer_blocks=4});if not r then return end
 for i=0,3 do local b=C.call('/fs','read',{file=notes.resolve('../brand/ready.adpcm'),offset=tostring(i*331),length=331});if b then C.call('/dev/speaker','write',{playback_id=r.playback_id,sequence=i,hex=b.hex}) end end
 C.call('/dev/speaker','finish',{playback_id=r.playback_id})
end
function C.prepare_sd(done)
 local s,e=C.call('/dev/sd','read');if not s then if done then done(nil,e) end return end
 if s.mounted then if done then done(s) end return end
 if s.operation_pending then if done then done(nil,'operation_active') end return end
 return notes.request('/dev/sd','mount',{},function(e) local state=C.call('/dev/sd','read');if done then done(state,e.event=='operation_failed' and e.data.error or nil) end end)
end
function C.init()
 C.preferences=C.read_json(preferences) or C.preferences
 actions.home=C.home
 local cfg=C.call('/sys/config','read',{scope='system'});local ui=cfg and cfg.effective and cfg.effective['/sys/ui'] or {}
 if not ui.font_file or ui.font_file=='' then
  C.call('/sys/config','write',{scope='system',persist=true,changes={{resource='/sys/ui',settings={font_file=notes.resolve('../fonts/NotesSansSC-Regular.ttf')}}}})
 end
 C.call('/sys/storage','configure',{provider='ram',quota_bytes=C.config.ram_file_quota})
 notes.on('/sys/timers','fired',function(e) local fn=timers[e.data.id];if fn then fn(e) end end)
 notes.on('/dev/touch','completed',function(e)
  touchPending=false
  if e.data.count>0 and not down then local p=e.data.contacts[1];local hit=C.call('/sys/ui','hit_test',{view_id=view,x=p.x,y=p.y});if hit and hit.revision>=C.layoutRevision then for _,id in ipairs(hit.ids) do if actions[id] then actions[id](e);break end end end end
  down=e.data.count>0
 end)
 notes.on('/dev/touch','failed',function() touchPending=false end)
 C.timer(C.config.touch_interval_ms,C.config.touch_interval_ms,function()
  if not touchPending and view then local r=C.call('/dev/touch','measure');touchPending=r~=nil end
 end)
end
return C
