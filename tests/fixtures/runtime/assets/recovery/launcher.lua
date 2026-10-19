-- Ordinary recovery launcher; native control and writable packages share public APIs.
local view,selected= nil,1
local entries={}
local function show(message)
 local listed=notes.call('/sys/apps','list',{cursor=0,limit=4});entries=listed.ok and listed.result.applications or {}
 local wifi=notes.call('/dev/wifi','read');local w=wifi.ok and wifi.result or {}
 local lines={};for i,item in ipairs(entries) do lines[#lines+1]=(i==selected and '> ' or '  ')..item.title end
 local r=notes.call('/sys/ui',view and 'update' or 'open',{view_id=view,path=notes.resolve('ui.json'),driver='e6.blue-yellow.v1',values={apps=table.concat(lines,'\n'),address='http://'..(w.connected and w.ip or w.ap_ip or '192.168.4.1'),message=message or ''}})
 if r.ok then view=r.result.view_id end
end
function on_start()
 notes.on('/dev/buttons','click',function(e)
  if e.data.button=='B' then local app=entries[selected];if app then local r=notes.call('/sys/apps','launch',{id=app.id});if not r.ok then show(r.error) end end
  else selected=(selected-1+(e.data.button=='C' and 1 or math.max(1,#entries)-1))%math.max(1,#entries)+1;show() end
 end)
 notes.on('/dev/buttons','long_press',function(e) if e.data.button=='C' then local r=notes.call('/dev/wifi','configure',{settings={ap_enabled=true}});show(r.ok and 'AP ready' or r.error) end end)
 notes.on('/dev/wifi',nil,function() show() end)
 show('Open web settings to repair the startup files.')
end
