-- Local timer + HTTP processing: no Harness invocation on each refresh.
local url='http://192.168.0.2:8080/status'
local job=nil
local timer=nil
function on_start()
  notes.subscribe('/sys/timers');notes.subscribe('/net/http')
  local r=notes.call('/sys/timers','write',{delay_ms=1000,period_ms=300000});if r.ok then timer=r.result.id end
end
function on_event(e)
  if e.source=='/sys/timers' and e.event=='fired' and e.data.id==timer and not job then
    local r=notes.call('/net/http','request',{url=url,response_limit=4096})
    if r.ok then job=r.result.job_id end
  elseif e.source=='/net/http' and e.data.job_id==job and (e.event=='completed' or e.event=='failed') then
    local r=notes.call('/net/http','read');job=nil
    if r.ok and r.result.http_ok then print(r.result.body) end
    -- Application code can decode the response and update a path-addressed UI here.
  end
end
