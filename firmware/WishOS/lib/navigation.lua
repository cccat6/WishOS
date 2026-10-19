-- Optional application navigation using published input facts and rendered targets.
-- Device packages choose roles/timing. Protected Home/Voice remain system-owned.
local M={}
function M.bind(options)
 local call=options.call;local buttons=call(options.source or '/dev/buttons','read')
 if not buttons or not buttons.buttons then return {stop=function()end}end
 local keys={};for name,b in pairs(buttons.buttons)do keys[#keys+1]={name=name,index=b.index,timing=b.applied or b.configured or {}}end
 table.sort(keys,function(a,b)return a.index<b.index end)
 local cfg=options.config or {};local roles=cfg.roles or {};local defaultMode=#keys==1 and (keys[1].timing.max_clicks or 1)<2 and 'scan'or 'direct';local mode=cfg.mode or defaultMode;assert(mode=='direct'or mode=='scan','navigation mode')
 local focus;local focusRevision=0;local view;local layout;local timer;local listener
 local function targets()
  local current=options.view();if not current then return end
  local result={};local cursor=0;local stamp
  repeat
   local r=call('/sys/ui','elements',{view_id=current,cursor=cursor,limit=8});if not r or not options.accept({presentation_id=r.view_id,presentation_revision=r.revision,presentation_activation=r.activation})then return end
   if stamp and stamp~=r.revision then return end;stamp=r.revision
   for _,n in ipairs(r.elements)do if n.target and options.actions[n.id]then result[#result+1]=n end end
   cursor=r.cursor;if not r.more then break end
  until false
  local last={};for i,id in ipairs(cfg.last or {})do last[id]=i end;table.sort(result,function(a,b)if (last[a.id]or 0)~=(last[b.id]or 0)then return (last[a.id]or 0)<(last[b.id]or 0)end;if a.y==b.y then return a.x<b.x end;return a.y<b.y end)
  local revision=options.layout and options.layout()or current;if view~=current or layout~=revision then view=current;layout=revision;focus=nil end
  return result,current,stamp
 end
 local function move(delta)
  local list,id=targets();if not list or #list==0 then return end
  local at=type(delta)=='number'and delta>0 and 0 or 1;for i,n in ipairs(list)do if n.id==focus then at=i;break end end
  local selected
  if type(delta)=='number'then selected=(at-1+delta)%#list+1
  else
   local origin=list[math.max(1,at)];local best
   for i,n in ipairs(list)do
    local x=n.x+(n.width or 0)/2-origin.x-(origin.width or 0)/2;local y=n.y+(n.height or 0)/2-origin.y-(origin.height or 0)/2
    local forward=delta=='left'and -x or delta=='right'and x or delta=='up'and -y or y
    local distance=x*x+y*y;if forward>0 and(not best or distance<best)then selected=i;best=distance end
   end
   if not selected then return end
  end
  local selectedId=list[selected].id;local r=call('/sys/ui','update',{view_id=id,focus=selectedId});if r then focus=selectedId;focusRevision=r.revision end
 end
 local function activate(event)
  local list,_,revision=targets();if not list or revision<focusRevision then return end
  for _,n in ipairs(list)do if n.id==focus then options.actions[focus](event);return end end
  move(1)
 end
 local function action(name,event)
  if name=='left'or name=='right'or name=='up'or name=='down'then move(name)elseif name=='next'then move(1)elseif name=='previous'then move(-1)elseif name=='activate'then activate(event)elseif name=='back'and options.back then options.back(event)end
 end
 listener=notes.on(options.source or '/dev/buttons','click',function(e)
  local d=e.data;if d.long or d.system_consumed or not options.accept(d)then return end
  for i,key in ipairs(keys)do if key.name==d.button then
   local role=roles[key.name];local name=role and role[tostring(d.count)]
   if not role then
    if mode=='scan'then if d.count==1 then name='activate'end
    elseif #keys==1 then name=d.count==1 and 'next'or d.count==2 and 'activate'
    elseif #keys==2 then name=d.count==1 and (i==1 and 'next'or 'activate')or i==2 and d.count==2 and 'back'
    else name=d.count==1 and (i==1 and 'previous'or i==#keys and 'activate'or 'next')or i==#keys and d.count==2 and 'back'end
   end
   action(name,e);return
  end end
 end)
 if mode=='scan'then timer=options.timer(cfg.scan_interval_ms or 1500,cfg.scan_interval_ms or 1500,function()move(1)end)end
 return {move=move,activate=activate,navigate=action,stop=function()notes.off(listener);if timer then options.cancel(timer)end end}
end
-- A normalized slider is an input capability, independent of screen coordinates.
-- Bind only the source/event advertised by its adapter. One step per detent/delta.
function M.axis(source,event,options,callback)
 options=options or {};local anchor
 local token=notes.on(source,event,function(e)
  local d=e.data;if options.axis and d.axis~=options.axis then return end;if d.phase=='cancel'then anchor=nil;return end
  if d.delta==0 then return end;if d.delta then callback(d.delta>0 and 'next'or 'previous',e);return end
  if type(d.position)~='number'then return end
  if d.phase=='begin'or anchor==nil then anchor=d.position;return end
  local delta=d.position-anchor;if math.abs(delta)>=(options.step or .15)then anchor=d.position;callback(delta>0 and 'next'or 'previous',e)end
  if d.phase=='end'then anchor=nil end
 end)
 return {stop=function()notes.off(token)end}
end
return M
