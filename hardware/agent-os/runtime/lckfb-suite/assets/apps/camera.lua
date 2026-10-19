local C=notes.require('../suite/common.lua');local P=notes.require('../suite/camera-preview.lua');local saveTimer,partial;local savePreparing=false
local function status(text) C.update({status=text}) end
local function camera_error(detail)
 if detail=='camera_memory_budget' or detail=='native_operation_memory_budget' or detail=='native_task_allocation_failed' then return C.t('资源不足，请稍后重试','Resources busy; retry later') end
 return detail or 'camera_failed'
end
local function save()
 local lastFrame=P.current();if not lastFrame then status(C.t('先点拍照定格','Freeze a frame before saving'));return end;if saveTimer or savePreparing then return end
 if partial then local removed,err=C.call('/fs','remove',{file=partial});if not removed and err~='not_found' then status(err);return end;partial=nil end
 savePreparing=true;status(C.t('正在准备保存','Preparing to save'))
 C.prepare_sd(function(s,e)
  savePreparing=false
  if not s or not s.mounted then status(e or C.t('请插入 SD 卡','Insert an SD card'));return end
  local folder='/storage/user/WishOS';local stat=C.call('/fs','stat',{file=folder});if not stat then local made,err=C.call('/fs','mkdir',{file=folder});if not made then status(err);return end end
  folder=folder..'/photos';if not C.call('/fs','stat',{file=folder}) then local made,err=C.call('/fs','mkdir',{file=folder});if not made then status(err);return end end
  local clock=C.call('/sys/clock','read') or {};local name=folder..'/capture-'..tostring(clock.unix_s or 0)..'-'..tostring(clock.monotonic_ms or 0)..'.bin';local src=lastFrame;local at=0
  partial=name..'.partial';local made,err=C.call('/fs','create',{file=partial});if not made then partial=nil;status(err);return end
  status(C.t('正在保存到 SD','Saving to SD'))
  local clock=C.call('/sys/clock') or {};local deadline=(clock.monotonic_ms or 0)+C.config.photo_save_timeout_ms
  saveTimer=C.timer(1,20,function()
   local now=C.call('/sys/clock') or {};if (now.monotonic_ms or 0)>=deadline then C.cancel(saveTimer);saveTimer=nil;status(C.t('保存超时，可重试','Save timed out; try again'));return end
   local b,err=C.call('/fs','read',{file=src,offset=tostring(at),length=1024});if not b then C.cancel(saveTimer);saveTimer=nil;status(err);return end
   if b.hex=='' then local flushed,err=C.call('/fs','flush',{file=partial});if flushed then flushed,err=C.call('/fs','rename',{file=partial,to=name}) end;C.cancel(saveTimer);saveTimer=nil;if flushed then partial=nil;status(C.t('已保存：WishOS/photos','Saved: WishOS/photos')) else status(err) end;return end
   local w,err=C.call('/fs','write',{file=partial,offset=tostring(at),hex=b.hex});if not w then if err=='bus_busy' or err=='spi_busy' or err=='resource_busy' then return end;C.cancel(saveTimer);saveTimer=nil;status(err);return end
   at=tonumber(b.next_offset)
  end)
 end)
end
function on_start()
 C.init();C.open('camera',{status=C.t('正在准备预览','Preparing preview'),capture_label=C.t('拍照','Capture'),save_label=C.t('定格后保存','Freeze to save')})
 P.init(C,function(state,detail)if state=='failed'then status(camera_error(detail))elseif state=='starting'then status(C.t('正在准备相机','Preparing camera'))end end)
 C.action('capture',function()if not saveTimer and not savePreparing then P.toggle()end end);C.action('save',save);P.start()
end
function on_stop()C.cancel(saveTimer);P.stop();if partial then C.call('/fs','remove',{file=partial})end end
