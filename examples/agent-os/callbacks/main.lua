local view

function on_start()
  notes.on('/dev/buttons', 'click', function(event)
    notes.call('/sys/ui', 'update', {
      view_id = view,
      values = {message = 'Button ' .. tostring(event.data.button)}
    })
  end)

  local reply = notes.request('/sys/ui', 'open', {
    path = notes.resolve('ui.json')
  }, function(event)
    if event.event == 'completed' then
      print('Shown: ' .. event.data.completion.kind)
    else
      print('Presentation ended: ' .. event.data.state)
    end
  end)
  assert(reply.ok, reply.error)
  view = reply.result.view_id
end
