function on_start()
 local r=notes.call('/dev/wifi','read');local w=r.ok and r.result or {}
 if not w.connected and not w.ap_active then notes.call('/dev/wifi','configure',{settings={enabled=true,ap_enabled=true}});r=notes.call('/dev/wifi','read');w=r.ok and r.result or {} end
 notes.call('/sys/ui','open',{path='/system/recovery/ui.json',values={url='http://'..(w.connected and w.ip or w.ap_ip or '192.168.4.1')..'/',network=w.connected and w.ssid or w.ap_ssid or ''}})
end
