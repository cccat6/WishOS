-- Decorative frames follow native display receipts, independently of task progress.
local A={};local timer;local common;local pendingView,pendingRevision
function A.init(C)
 common=C
 notes.on('/dev/display',{'completed','failed'},function(e)
  if e.data.view_id==pendingView and e.data.view_revision==pendingRevision then pendingView=nil;pendingRevision=nil end
 end)
end
function A.stop()if timer then common.cancel(timer);timer=nil end;pendingView=nil;pendingRevision=nil end
function A.start(phase)
 A.stop();local frame=0
 timer=common.timer(1,common.config.animation_interval_ms or 120,function()
  if pendingRevision then return end
  local shown=phase=='done' and ({4,7,11})[frame+1] or frame
  local r=common.update({activity=notes.resolve('../brand/'..phase..'-'..string.format('%02d',shown)..'.png')})
  if not r then A.stop();return end
  if r.revision then pendingView=common.view;pendingRevision=r.revision end
  frame=frame+1;if phase=='done' and frame==3 then A.stop()else frame=frame%12 end
 end)
end
return A
