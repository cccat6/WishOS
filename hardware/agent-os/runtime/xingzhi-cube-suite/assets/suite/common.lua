local C={};C.config={boot_logo_ms=400,recording={sample_rate=16000,max_ms=300000,buffer_blocks=256,delivery_ttl_ms=30000},voice_timeout_ms=900000,session_timeout_ms=15000,animation_interval_ms=120};C.preferences={language='zh'};local handlers={};local pref=notes.resolve('../preferences.json')
function C.call(p,m,a) local r=notes.call(p,m or 'read',a or {});if r.ok then return r.result end;return nil,r.error end
local function unhex(s) return notes.unhex(s) end
function C.change_preferences(values,removed) local n={};for k,v in pairs(C.preferences)do n[k]=v end;for k,v in pairs(values or {})do n[k]=v end;for _,k in ipairs(removed or {})do n[k]=nil end;local r,e=C.call('/fs','write',{file=pref,text=notes.encode(n),truncate=true});if r then C.preferences=n end;return r,e end
function C.t(zh,en)return C.preferences.language=='en' and en or zh end
function C.open(values,page,extraHidden) local hidden=values and values.title=='WishOS'and {'title'}or {'brand','brand_name'};for _,id in ipairs(extraHidden or {})do hidden[#hidden+1]=id end;local r,e=C.call('/sys/ui','open',{path=notes.resolve('../ui/'..(page or 'home')..'.json'),history='replace',values=values or {},hidden=notes.array(hidden)});assert(r,e);C.view=r.view_id;C.layoutRevision=r.revision;C.layoutActivation=r.activation;C.values=values or {} end
function C.accept_input(data)
 if data.presentation_id and data.presentation_id~=C.view then return false end
 if data.presentation_revision and data.presentation_revision>0 and C.layoutRevision and data.presentation_revision<C.layoutRevision then return false end
 if data.presentation_activation and data.presentation_activation>0 and C.layoutActivation and data.presentation_activation~=C.layoutActivation then return false end
 return true
end
-- Native click facts combine the sequence before this ordinary app chooses
-- meanings. The driver exposes the configured click window and cancels on hold.
function C.click(data,next,confirm)
 if data.button~='USER' or data.long or data.system_consumed or not C.accept_input(data) then return end
 if data.count==1 then next()elseif data.count==2 then confirm()end
end
function C.update(values)local changed=false;for k,v in pairs(values)do if not C.values or C.values[k]~=v then changed=true;break end end;if not changed then return {} end;local n={};for k,v in pairs(C.values or {})do n[k]=v end;for k,v in pairs(values)do n[k]=v end;local r,e=C.call('/sys/ui','update',{view_id=C.view,values=n});if r then C.values=n end;return r,e end
function C.timer(delay,period,fn)local r,e=C.call('/sys/timers','write',{delay_ms=delay,period_ms=period});if r then handlers[r.id]=fn end;return r and r.id,e end
function C.cancel(id)if id then handlers[id]=nil;C.call('/sys/timers','remove',{id=id})end end
function C.init()local f=C.call('/fs','read',{file=pref,length=1024});if f then local ok,v=pcall(notes.decode,unhex(f.hex));if ok then C.preferences=v end end;notes.on('/sys/timers','fired',function(e)local f=handlers[e.data.id];if f then f(e)end end)end
function C.choice_preview(titles,index)
 local count=#titles;if count<2 then return {previous_choice='',next_choice=''} end
 return {previous_choice=titles[(index-2)%count+1],next_choice=titles[index%count+1]}
end
function C.status()local t=C.call('/services/time') or {};local p=C.call('/dev/battery') or {};return {clock=t.wall_clock_valid and t.local_time:sub(12,16) or '',power=p.valid and tostring(p.battery_percent)..'%' or ''}end
return C
