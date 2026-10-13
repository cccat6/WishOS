-- Any local event can request a remote service or Harness task.
function on_start()
  notes.subscribe('/dev/buttons');notes.subscribe('/services/remote')
end
function on_event(e)
  if e.source=='/dev/buttons' and e.event=='press' and e.data.button=='B' then
    notes.call('/services/remote','submit',{route='agent',payload={text='Update the current dashboard'},timeout_ms=300000})
  elseif e.source=='/services/remote' and (e.event=='completed' or e.event=='failed') then
    local result=notes.call('/services/remote','read',{id=e.data.id})
    print(notes.encode(result))
    -- A Harness acceptance receipt includes run_id. Poll agent_status as a service,
    -- or let the Harness push the final display using hardware_invoke.
    notes.call('/services/remote','release',{id=e.data.id})
  end
end
