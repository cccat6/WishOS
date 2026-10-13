-- Example policy, not a hardware rule. Replace this source at runtime.
notes.subscribe('/dev/buttons')
function on_event(e)
  local key = e.data.button
  if key == 'B' and e.event == 'press' then
    notes.call('/dev/mic', 'start', {storage='retry'})
    return true
  elseif key == 'B' and e.event == 'release' then
    notes.call('/dev/mic', 'stop')
    return true
  elseif key == 'C' and e.event == 'press' then
    notes.call('/sys/ui', 'refresh')
    return true
  elseif key == 'A' and e.event == 'press' then
    notes.call('/sys/ui', 'back')
    return true
  end
end
