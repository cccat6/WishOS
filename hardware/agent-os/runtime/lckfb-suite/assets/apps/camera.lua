local C=notes.require('../suite/common.lua');local frame,file,saveTimer;local candidateOwned=false;local busy=false;local lastFrame;local garbage={};local partial
local function status(text) C.update({status=text}) end
local function clear(id) if id then C.call('/dev/camera','release',{frame_id=id}) end end
local function release_camera()
 clear(frame);frame=nil
 local state=C.call('/dev/camera','read')
 if state and state.ready and not state.operation_pending then
  local stopped=notes.request('/dev/camera','stop',{},function(e) busy=false;if e.event~='operation_completed' then status(e.data.error or 'camera_stop_failed') end end)
  if not stopped.ok then busy=false end
 else busy=false end
end
local function failed_capture(error)
 if candidateOwned and file and file~=lastFrame then C.call('/fs','remove',{file=file}) end
 candidateOwned=false;file=nil;release_camera();status(error)
end
local function capture_ready()
 if busy or saveTimer then return end;busy=true;file=nil;candidateOwned=false;status(C.t('正在拍照','Capturing'))
 local r=notes.request('/dev/camera','capture',{},function(e)
  if e.event~='operation_completed' then failed_capture(e.data.error or 'camera_capture_failed');return end
  local s=C.call('/dev/camera','read');if not s or not s.frame_id or s.frame_id==0 then failed_capture('camera_frame_missing');return end
  frame=s.frame_id;file='/storage/ram/wishos-camera-'..frame..'.bin'
  local w,h=s.width,s.height;local header=string.format('19120000%02x%02x%02x%02x%02x%02x0000',w%256,w//256,h%256,h//256,(w*2)%256,(w*2)//256)
  local created,err=C.call('/fs','create',{file=file,temporary=true});if not created then failed_capture(err);return end;candidateOwned=true
  local ready,err=C.call('/fs','write',{file=file,hex=header});if not ready then failed_capture(err);return end
  local copied,err=C.call('/dev/camera','copy_frame',{frame_id=frame,file=file,offset='12',pixel_format='rgb565le'})
  if not copied then failed_capture(err);return end
  release_camera()
  local r,e=C.update({frame=file,status=C.t('已拍摄 · 可保存到 SD 卡','Captured · Save to SD if desired')});if r then if lastFrame then garbage[#garbage+1]=lastFrame end;lastFrame=file else C.call('/fs','remove',{file=file});status(e) end
 end)
 if not r.ok then failed_capture(r.error) end
end
local function capture()
 if busy or saveTimer then return end
 local s,e=C.call('/dev/camera','read');if not s then status(e);return end
 if s.operation_pending then status(C.t('相机正在准备，请稍后再试','Camera is preparing; try again shortly'));return end
 if s.ready then capture_ready();return end
 busy=true;status(C.t('正在准备相机','Preparing camera'))
 local configured,e=C.call('/dev/camera','configure',{frame_size='qvga'});if not configured then busy=false;status(e);return end
 local r=notes.request('/dev/camera','start',{},function(e) busy=false;if e.event=='operation_completed' then capture_ready() else status(e.data.error or 'camera_start_failed') end end)
 if not r.ok then busy=false;status(r.error) end
end
local function save()
 if not lastFrame or saveTimer then return end
 C.prepare_sd(function(s,e)
  if not s or not s.mounted then status(e or C.t('请插入 SD 卡','Insert an SD card'));return end
  local folder='/storage/user/WishOS';local stat=C.call('/fs','stat',{file=folder});if not stat then local made,err=C.call('/fs','mkdir',{file=folder});if not made then status(err);return end end
  folder=folder..'/photos';if not C.call('/fs','stat',{file=folder}) then local made,err=C.call('/fs','mkdir',{file=folder});if not made then status(err);return end end
  local clock=C.call('/sys/clock','read') or {};local name=folder..'/capture-'..tostring(clock.unix_s or 0)..'-'..tostring(clock.monotonic_ms or 0)..'.bin';local src=lastFrame;local at=0
  partial=name..'.partial';local made,err=C.call('/fs','create',{file=partial});if not made then partial=nil;status(err);return end
  status(C.t('正在保存到 SD','Saving to SD'))
  saveTimer=C.timer(1,20,function()
   local b,err=C.call('/fs','read',{file=src,offset=tostring(at),length=1024});if not b then C.cancel(saveTimer);saveTimer=nil;status(err);return end
   if b.hex=='' then local flushed,err=C.call('/fs','flush',{file=partial});if flushed then flushed,err=C.call('/fs','rename',{file=partial,to=name}) end;C.cancel(saveTimer);saveTimer=nil;if flushed then partial=nil;status(C.t('已保存：WishOS/photos','Saved: WishOS/photos')) else status(err) end;return end
   local w,err=C.call('/fs','write',{file=partial,offset=tostring(at),hex=b.hex});if not w then C.cancel(saveTimer);saveTimer=nil;status(err);return end
   at=tonumber(b.next_offset)
  end)
 end)
end
function on_start()
 C.init();C.open('camera',{status=C.t('点击拍照，可保存到 SD','Tap Capture, then Save to SD')});C.action('capture',capture);C.action('save',save);C.button(function() C.launch('voice',{record=true}) end)
 notes.on('/dev/display','completed',function() for i=#garbage,1,-1 do local r,e=C.call('/fs','remove',{file=garbage[i]});if r or e=='not_found' then table.remove(garbage,i) end end end)
end
function on_stop()
 C.cancel(saveTimer);clear(frame);if partial then C.call('/fs','remove',{file=partial}) end
 if candidateOwned and file and file~=lastFrame then C.call('/fs','remove',{file=file}) end
 if lastFrame then C.call('/fs','remove',{file=lastFrame}) end
end
