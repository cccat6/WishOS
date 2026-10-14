#pragma once
#include <stdint.h>
namespace notesos {
// Lazy runtime library. Its closures live in the VM budget, not a native fixed
// handler array; simple/isolated programs do not instantiate it.
inline constexpr char CALLBACK_LIBRARY[]=R"lua(
local listen,call,schedule,null,request,receipt=...
local handlers,sources={},{}
local count,serial=0,0
local M={}
local function scalar(v)
 local t=type(v);return t=='string' or t=='number' or t=='boolean' or v==null
end
function M.on(source,event,fn,options)
 assert(type(source)=='string' and type(fn)=='function','callback_parameters')
 assert(options==nil or type(options)=='table','callback_options');options=options or {}
 for k in pairs(options) do assert(k=='once' or k=='where','unknown_callback_option') end
 assert(options.once==nil or type(options.once)=='boolean','callback_once_type')
 local events
 if event~=nil then
  events={}
  if type(event)=='string' then assert(#event>0 and #event<40,'callback_event');events[event]=true
  else
   assert(type(event)=='table' and #event>0,'callback_events')
   local n=0;for k,v in pairs(event) do n=n+1;assert(math.type(k)=='integer' and k>=1 and k<=#event and type(v)=='string' and #v>0 and #v<40,'callback_events');events[v]=true end
   assert(n==#event,'callback_events')
  end
 end
 local where={};assert(options.where==nil or type(options.where)=='table','callback_where')
 for k,v in pairs(options.where or {}) do assert(type(k)=='string' and scalar(v),'callback_where_scalar');where[k]=v end
 serial=serial+1
 local h={id=serial,source=source,events=events,fn=fn,where=where,once=options.once==true}
 h.after=listen(source,true,count+1)
 handlers[h.id]=h;sources[source]=(sources[source] or 0)+1;count=count+1
 return h.id
end
function M.off(id)
 local h=handlers[id];if not h then return false end
 if h.receipt then receipt(h.receipt,true) end
 handlers[id]=nil;count=count-1
 local n=sources[h.source]-1;sources[h.source]=n>0 and n or nil
 listen(h.source,n>0,count)
 return true
end
local function matches(h,e)
 local delta=(e.sequence-h.after)&0xffffffff
 if delta==0 or delta>=0x80000000 or h.source~=e.source or (h.events and not h.events[e.event]) then return false end
 for k,v in pairs(h.where) do if e.data[k]~=v then return false end end
 return true
end
local function invoke(h,e)
 if handlers[h.id]~=h then return false end
 if h.once then M.off(h.id) end
 return h.fn(e)==true
end
function M.dispatch(e,legacy)
 local snapshot={}
 for _,h in pairs(handlers) do if matches(h,e) then snapshot[#snapshot+1]=h end end
 table.sort(snapshot,function(a,b)return a.id<b.id end)
 local consumed=false
 for _,h in ipairs(snapshot) do if invoke(h,e) then consumed=true end end
 if not consumed and type(legacy)=='function' then return legacy(e) end
 return consumed
end
function M.request(path,method,args,fn)
 assert(type(fn)=='function','callback_function_required')
 local d=call('/sys/capabilities','describe',{path=path})
 if not d.ok then return d end
 local spec=d.result.async and d.result.async[method]
 if not spec then return {ok=false,error='async_not_declared'} end
 local where={};for k in pairs(spec.match) do where[k]=false end
 local token=M.on(spec.source,spec.events,fn,{once=true,where=where})
 -- Reserve notification state before the operation can have side effects.
 local ok,r=pcall(request,path,method,args)
 if not ok then M.off(token);error(r) end
 if not r.ok then M.off(token);return r end
 local h=handlers[token];h.receipt=r.result.completion_id
 for k,field in pairs(spec.match) do
  local value=r.result[field]
  assert(value~=nil and scalar(value),'async_reply_missing_correlation:'..field)
  h.where[k]=value
 end
 schedule()
 return r,token
end
function M.flush()
 local batch={}
 for _,h in pairs(handlers) do if h.receipt then local e=receipt(h.receipt,false);if e.ready then batch[#batch+1]={h=h,e=e} end end end
 table.sort(batch,function(a,b)return a.e.completion_order<b.e.completion_order end)
 for _,item in ipairs(batch) do invoke(item.h,item.e) end
end

return M
)lua";
inline constexpr uint64_t callbackSourceHash(){uint64_t value=14695981039346656037ULL;for(unsigned i=0;i<sizeof(CALLBACK_LIBRARY)-1;++i)value=(value^uint8_t(CALLBACK_LIBRARY[i]))*1099511628211ULL;return value;}
}
