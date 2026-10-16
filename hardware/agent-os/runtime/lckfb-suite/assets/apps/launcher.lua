-- Ordinary replaceable launcher; all device calls use public resources.
local view
local function refresh()
 local response=notes.call('/dev/wifi','read',{})
 local w=response.ok and response.result or {}
 local network=w.connected and ((w.ssid or '')..' · '..(w.ip or '')) or ('AP '..(w.ap_ssid or '')..'\n'..(w.ap_ip or '192.168.4.1'))
 if view then notes.call('/sys/ui','update',{view_id=view,values={network=network}}) end
end
function on_start()
 local r=notes.call('/sys/ui','open',{path=notes.resolve('../ui/home.json'),history='replace'})
 assert(r.ok,r.error);view=r.result.view_id
 notes.call('/net/http-server','route',{path='/',content_type='text/html; charset=utf-8',body='<meta charset="utf-8"><meta http-equiv="refresh" content="0;url=/control/setup.html"><a href="/control/setup.html">WishOS</a>'})
 notes.on('/dev/wifi',{'connected','disconnected','changed'},refresh)
 refresh()
end
