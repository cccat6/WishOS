-- Any local event can request a remote service or Harness task.
function on_start()
  notes.subscribe('/dev/buttons');notes.subscribe('/services/ai')
end
function on_event(e)
  if e.source=='/dev/buttons' and e.event=='press' and e.data.button=='B' then
    notes.call('/services/ai','submit',{input={text='Update the current dashboard'},timeout_ms=300000})
  elseif e.source=='/services/ai' and e.event=='application_action' then
    local action=notes.call('/services/ai','read_action',{id=e.data.id,step=e.data.step})
    if action.ok then
      print(notes.encode(action.result.action.data))
      notes.call('/services/ai','acknowledge',{id=e.data.id,step=e.data.step,ok=true})
    elseif e.data.data then print(notes.encode(e.data.data)) end
  elseif e.source=='/services/ai' and (e.event=='completed' or e.event=='failed') then
    local result=notes.call('/services/ai','read',{id=e.data.id})
    print(notes.encode(result))
    -- The system has processed the ordered application/system actions.
    -- A replacement app may already have taken over this instance.
    notes.call('/services/ai','release',{id=e.data.id})
  end
end
