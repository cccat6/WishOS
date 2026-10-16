-- 本应用选择请求 Harness；系统执行有序 system/application 指令。
local receiver=notes.require('/system/lib/ai.lua')
function on_start()
 receiver.listen(function(action)
  print(notes.encode(action.data or {}))
 end)
 notes.on('/dev/buttons','press',function(e)
  if e.data.button~='B' then return end
  local r=notes.request('/services/ai','submit',{input={text='Update the current dashboard'},timeout_ms=300000},function(e)
   local result=notes.call('/services/ai','read',{id=e.data.id})
   print(notes.encode(result))
   notes.call('/services/ai','release',{id=e.data.id})
  end)
  if not r.ok then print(r.error) end
 end)
end
