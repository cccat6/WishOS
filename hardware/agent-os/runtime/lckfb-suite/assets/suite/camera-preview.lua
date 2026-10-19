-- Ordinary camera application pipeline: retained capture, volatile frame,
-- completed presentation, then the next capture. No native UI workflow.
local P={};local C,notify;local running=false;local frozen=false;local busy=false;local stopRequested=false;local started=false
local frame,candidate,visible,presentToken;local files={};local generation=0
local function remove(file)
 if not file then return end
 local r,e=C.call('/fs','remove',{file=file});if r or e=='not_found' then files[file]=nil end
end
local function release()
 if frame then C.call('/dev/camera','release',{frame_id=frame});frame=nil end
end
local function stop_camera()
 if not started then return end
 local s=C.call('/dev/camera');if s and s.ready and not s.operation_pending then
  notes.request('/dev/camera','stop',{},function(e)if e.event=='operation_completed'then started=false else notify('failed',e.data.error or 'camera_stop_failed')end end)
 end
end
local function failure(error)
 running=false;busy=false;release();if candidate and candidate~=visible then remove(candidate)end;candidate=nil
 if visible then C.model.frame=visible else C.model.frame=notes.resolve('../icons/camera.png')end
 stop_camera();C.update({capture_label=C.t('重试预览','Retry preview'),save_label=visible and C.t('保存上一帧','Save last frame')or C.t('定格后保存','Freeze to save')});notify('failed',error)
end
local capture
local function present(file,epoch)
 local values={};for k,v in pairs(C.model)do values[k]=v end;values.frame=file
 values.status=frozen and C.t('已定格 · 可保存到 SD','Frozen · Save to SD')or C.t('实时预览 · 点拍照定格','Preview · Tap Capture to freeze')
 values.capture_label=frozen and C.t('继续预览','Resume preview')or C.t('拍照','Capture');values.save_label=frozen and C.t('保存到 SD','Save to SD')or C.t('定格后保存','Freeze to save')
 local r,token=notes.request('/sys/ui','update',{view_id=C.view,values=values},function(e)
  presentToken=nil;if epoch~=generation then return end
  if e.event~='completed'then failure(e.data.error or 'preview_display_failed');return end
  local prior=visible;visible=file;candidate=nil;busy=false;if prior and prior~=visible then remove(prior)end
  if frozen then C.update({capture_label=C.t('继续预览','Resume preview'),save_label=C.t('保存到 SD','Save to SD'),status=C.t('已定格 · 可保存到 SD','Frozen · Save to SD')});notify('frozen',visible)elseif running then capture()end
 end)
 if not r.ok then failure(r.error);return end;presentToken=token;C.model=values
end
capture=function()
 if not running or frozen or busy then return end;busy=true;local epoch=generation
 local r=notes.request('/dev/camera','capture',{},function(e)
  if epoch~=generation then return end
  if e.event~='operation_completed'then failure(e.data.error or 'camera_capture_failed');return end
  local state=C.call('/dev/camera');if not state or not state.frame_id or state.frame_id==0 then failure('camera_frame_missing');return end
  frame=state.frame_id;candidate='/storage/ram/wishos-camera-'..frame..'.bin'
  local created,error=C.call('/fs','create',{file=candidate,temporary=true});if not created then failure(error);return end;files[candidate]=true
  local w,h=state.width,state.height;local header=string.format('19120000%02x%02x%02x%02x%02x%02x0000',w%256,w//256,h%256,h//256,(w*2)%256,(w*2)//256)
  local written,error=C.call('/fs','write',{file=candidate,hex=header});if not written then failure(error);return end
  local copied,error=C.call('/dev/camera','copy_frame',{frame_id=frame,file=candidate,offset='12',pixel_format='rgb565le'});release()
  if not copied then failure(error);return end
  if stopRequested then busy=false;remove(candidate);candidate=nil;stop_camera();return end
  present(candidate,epoch)
 end)
 if not r.ok then failure(r.error)end
end
function P.init(common,changed)C=common;notify=changed end
function P.start()
 if busy then return end;running=true;frozen=false;stopRequested=false;generation=generation+1
 local state,error=C.call('/dev/camera');if not state then failure(error);return end
 if state.ready then capture();return end
 busy=true;notify('starting');local epoch=generation
 local r=notes.request('/dev/camera','start',{},function(e)
  if epoch~=generation then return end;busy=false
  if e.event~='operation_completed'then failure(e.data.error or 'camera_start_failed');return end
  started=true;if stopRequested then stop_camera()else capture()end
 end)
 if not r.ok then failure(r.error)end
end
function P.toggle()
 if not running and not frozen then P.start();return end
 if frozen then if not busy then P.start()end;return end
 if not visible then notify('starting');return end
 frozen=true;running=false
 -- An already submitted frame completes normally. It becomes the frozen frame;
 -- the app never aborts DMA or modifies a file still used by presentation.
 if not busy then C.update({capture_label=C.t('继续预览','Resume preview'),save_label=C.t('保存到 SD','Save to SD'),status=C.t('已定格 · 可保存到 SD','Frozen · Save to SD')})end
 if not busy then notify('frozen',visible)end
end
function P.current()return not running and not busy and visible or nil end
function P.stop()
 stopRequested=true;running=false;generation=generation+1;if presentToken then notes.off(presentToken);presentToken=nil end
 release();for file in pairs(files)do remove(file)end
 -- Native owner cleanup safely finishes an accepted operation and returns its
 -- SDK frame. Temporary file retirement waits for outstanding display leases.
end
return P
