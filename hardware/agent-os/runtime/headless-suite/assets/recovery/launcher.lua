-- Ordinary web launcher on hardware without a physical display.
function on_start()
 local r=notes.call('/net/http-server','route',{path='/',content_type='text/html; charset=utf-8',body='<meta charset="utf-8"><meta http-equiv="refresh" content="0;url=/control/setup.html"><a href="/control/setup.html">WishOS</a>'})
 assert(r.ok,r.error)
end
