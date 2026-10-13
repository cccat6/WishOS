local page=notes.require('lib/page.lua')
local job=nil
function on_start()
  notes.subscribe('/dev/buttons')
  notes.subscribe('/sys/workers')
end
function on_event(e)
  if e.source=='/dev/buttons' and e.event=='press' and not job then
    local r=notes.call('/sys/workers','submit',{
      file=notes.resolve('compute.lua'),input={count=10000},core='auto',
      memory_bytes=32768,instruction_budget=1000000
    })
    if r.ok then job=r.result.id else print(r.error) end
  elseif e.source=='/sys/workers' and e.data.id==job then
    local r=notes.call('/sys/workers','read',{id=job})
    if r.ok then
      notes.call('/sys/workers','release',{id=job});job=nil
      if r.result.ok then
        local target='/storage/ram/local-result.json'
        local text=notes.encode(page.result(r.result.result.sum))
        local write=notes.call('/fs','write',{file=target,text=text,truncate=true})
        if write.ok then notes.call('/sys/ui','open',{path=target,history='replace'}) end
      else print(r.result.error) end
    end
  end
end
function on_stop()
  -- Cancel is a request. A supervisor releases the receipt after completion.
  if job then notes.call('/sys/workers','cancel',{id=job}) end
end
