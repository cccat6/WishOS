local locale=notes.require('../suite/locale.lua')
local view
local function start()
 local launch=notes.call('/sys/apps','read');local arguments=launch.ok and launch.result.arguments or {}
 if arguments.from~='browser' then
  local r=notes.call('/dev/wifi','read');local w=r.ok and r.result or {}
  notes.call('/sys/ui','open',{path=notes.resolve('../ui/network.json'),driver='e6.color-fast.v1',values=locale.values('network',{ssid=w.connected and w.ssid or locale.text('请先配网','Connect to Wi-Fi first'),ip=w.connected and w.ip or w.ap_ip,mac=w.mac or '',message=locale.text('浏览器打开后台，选择手机传图。长按 A 返回桌面。','Open Photo upload in the web launcher. Hold A for Home.'),ap=w.ap_ssid or ''})})
 end
 local r=notes.call('/sys/ui','create');assert(r.ok,r.error);view=r.result.view_id
 local response=notes.call('/net/http-server','route',{path='/app/photo/context',body=notes.encode({view_id=view}),content_type='application/json'})
 assert(response.ok,response.error)
end

function on_start() locale.restore(start) end
