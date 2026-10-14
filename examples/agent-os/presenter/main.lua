-- HID report construction and button semantics are application code.
local report_map='05010906a1018501050719e029e71500250175019508810295017508810195067508150025650507190029658100c0'
local ready=false
function on_start()
  notes.subscribe('/dev/buttons')
  local r=notes.request('/dev/ble','start',{mode='hid',name='Notes Presenter',report_map_hex=report_map},function(e) ready=e.event=='operation_completed' end)
  assert(r.ok,r.error)
end
function on_event(e)
  if not ready or e.source~='/dev/buttons' then return end
  local key=e.data.button=='A' and '4b' or e.data.button=='C' and '4e' or nil
  if not key then return end
  if e.event=='press' then notes.call('/dev/ble','write',{index=0,hex='0000'..key..'0000000000'})
  elseif e.event=='release' then notes.call('/dev/ble','write',{index=0,hex='0000000000000000'}) end
end
