-- Any local event can request a remote service or Harness task.
function on_start()
  notes.subscribe('/dev/buttons');notes.subscribe('/services/ai')
end
function on_event(e)
  if e.source=='/dev/buttons' and e.event=='press' and e.data.button=='B' then
    notes.call('/services/ai','submit',{target='both',input={text='Update the current dashboard'},timeout_ms=300000})
  elseif e.source=='/services/ai' and (e.event=='completed' or e.event=='failed') then
    local result=notes.call('/services/ai','read',{id=e.data.id})
    print(notes.encode(result))
    -- The system has processed the typed result and its declared effects.
    -- A replacement app may already have taken over this instance.
    notes.call('/services/ai','release',{id=e.data.id})
  end
end
