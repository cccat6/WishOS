local A={}
local locale=notes.require('locale.lua')
local cursor,selected,entries,more=0,1,{},false
local function load(at)
 local r=notes.call('/sys/apps','list',{cursor=at,limit=4})
 if not r.ok then return r.error end
 cursor=at;entries=r.result.applications or {};more=r.result.more or false
 selected=math.max(1,math.min(selected,#entries));return nil
end
function A.values()
 local error=load(cursor);local values={title=locale.text('应用','Apps'),message=error or locale.text('B 打开应用；长按 A 返回桌面','B opens app; hold A for Home')}
 for i=1,4 do values['item'..i]=entries[i] and ((i==selected and '> ' or '  ')..entries[i].title) or '' end
 values.item5='';values.item6='';return values
end
function A.move(delta)
 if delta>0 then
  if selected<#entries then selected=selected+1 elseif more then selected=1;load(cursor+4) else selected=1 end
 else
  if selected>1 then selected=selected-1 elseif cursor>0 then selected=4;load(cursor-4) else selected=math.max(1,#entries) end
 end
end
function A.open() local app=entries[selected];if app then return notes.call('/sys/apps','launch',{id=app.id}) end end
return A
