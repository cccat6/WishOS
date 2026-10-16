-- 本地定时请求；完成回执由系统保留，不依赖事件环是否覆盖。
local url='http://192.168.0.2:8080/status'
local job,timer
function on_start()
 notes.on('/sys/timers','fired',function(e)
  if e.data.id~=timer or job then return end
  local r=notes.request('/net/http','request',{url=url,response_limit=1024},function(e)
   local result=notes.call('/net/http','read',{job_id=e.data.job_id})
   notes.call('/net/http','release',{job_id=e.data.job_id});job=nil
   if result.ok and result.result.http_ok then print(result.result.body) end
   -- 在这里解析受限响应并更新应用自己的 UI。
  end)
  if r.ok then job=r.result.job_id else print(r.error) end
 end)
 local r=notes.call('/sys/timers','write',{delay_ms=1000,period_ms=300000})
 assert(r.ok,r.error);timer=r.result.id
end
