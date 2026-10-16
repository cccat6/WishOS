local C=notes.require('../suite/common.lua');local frame,file,previous,copyTimer,saveTimer;local offset,total=0,0;local busy=false;local lastFrame;local garbage={};local partial
local function status(text) C.update({status=text}) end
local function clear(id) if id then C.call('/dev/camera','release',{frame_id=id}) end end
local function capture()
 if busy or saveTimer then return end;busy=true;status(C.t('正在拍照','Capturing'))
 local r=notes.request('/dev/camera','capture',{},function(e)
  if e.event~='operation_completed' then busy=false;status(e.data.error or 'camera_capture_failed');return end
  local s=C.call('/dev/camera','read');if not s or not s.frame_id or s.frame_id==0 then busy=false;status('camera_frame_missing');return end
  frame=s.frame_id;total=s.bytes;offset=0;file='/storage/ram/wishos-camera-'..frame..'.bin'
  local w,h=s.width,s.height;local header=string.format('19120000%02x%02x%02x%02x%02x%02x0000',w%256,w//256,h%256,h//256,(w*2)%256,(w*2)//256)
  local created,err=C.call('/fs','create',{file=file,temporary=true});if not created then clear(frame);frame=nil;busy=false;status(err);return end
  local ready,err=C.call('/fs','write',{file=file,hex=header});if not ready then clear(frame);frame=nil;busy=false;status(err);return end
  copyTimer=C.timer(1,20,function()
   for _=1,4 do
    if offset>=total then
     C.cancel(copyTimer);copyTimer=nil;clear(frame);frame=nil;busy=false
     local r,e=C.update({frame=file,status=C.t('已拍摄 · 可保存到 SD 卡','Captured · Save to SD if desired')});if r then if lastFrame then garbage[#garbage+1]=lastFrame end;lastFrame=file else C.call('/fs','remove',{file=file});status(e) end;return
    end
    local b,err=C.call('/dev/camera','read_chunk',{frame_id=frame,offset=tostring(offset),length=1024});if not b then C.cancel(copyTimer);clear(frame);frame=nil;busy=false;status(err);return end
    -- Camera transport is RGB565BE; the generic LVGL file is RGB565LE.
    local words={};for i=1,#b.hex,4 do words[#words+1]=b.hex:sub(i+2,i+3)..b.hex:sub(i,i+1) end
    local q,err=C.call('/fs','write',{file=file,offset=tostring(offset+12),hex=table.concat(words)});if not q then C.cancel(copyTimer);clear(frame);frame=nil;busy=false;status(err);return end
    offset=tonumber(b.next_offset)
   end
  end)
 end)
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
 C.init();C.open('camera');C.action('capture',capture);C.action('save',save);C.button(capture)
 notes.on('/dev/display','completed',function() for i=#garbage,1,-1 do local r,e=C.call('/fs','remove',{file=garbage[i]});if r or e=='not_found' then table.remove(garbage,i) end end end)
 local s=C.call('/dev/camera','read')
 if s and s.ready then capture();return end
 local configured,e=C.call('/dev/camera','configure',{frame_size='qvga'});if not configured then status(e);return end
 local r=notes.request('/dev/camera','start',{},function(e) if e.event=='operation_completed' then capture() else status(e.data.error or 'camera_start_failed') end end)
 if not r.ok then status(r.error) end
end
function on_stop()
 C.cancel(copyTimer);C.cancel(saveTimer);clear(frame);if partial then C.call('/fs','remove',{file=partial}) end
 for _,path in ipairs({lastFrame or '',previous or '',file or ''}) do if path~='' then C.call('/fs','remove',{file=path}) end end
end
