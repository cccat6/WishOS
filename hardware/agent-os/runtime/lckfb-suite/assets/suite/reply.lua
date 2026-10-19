-- Optional reply audio uses normal file/output resources. Text stays available.
local R={};local C,notify;local playback,timer,offset,sequence,media,ack;local finished=false;local waiting;local playbackToken,timeoutTimer,pendingError
local function done(error)
 if timer then C.cancel(timer);timer=nil end;if timeoutTimer then C.cancel(timeoutTimer);timeoutTimer=nil end;playbackToken=nil
 local complete=ack;ack=nil;playback=nil;media=nil
 if notify and not waiting then notify(error and 'unavailable' or 'finished',error)end
 if complete then complete(true,error and {audio_error=error} or {played=true})end
 local next=waiting;waiting=nil;if next then local pending=R.play(next.audio,next.complete);if pending~=false then next.complete(true,{audio_skipped=true})end end
end
function R.init(common,onStatus)
 C=common;notify=onStatus
 local cap=C.call('/sys/capabilities','describe',{path='/dev/speaker'});R.available=cap~=nil
 notes.on('/dev/speaker','started',function(e)if e.data.playback_id==playback and notify then notify('playing')end end)
end
function R.mode()return R.available and (C.preferences.reply_mode or C.config.reply_mode or 'text') or 'text'end
function R.label()return R.mode()=='speech'and C.t('语音','Voice')or C.t('文字','Text')end
function R.context(reference)local input={};for k,v in pairs(reference or {})do input[k]=v end;input.reply={mode=R.mode()};return input end
function R.toggle()
 local mode=R.mode()=='speech'and 'text'or 'speech';local saved,e=C.change_preferences({reply_mode=mode});if saved and mode=='text'then R.stop()end;return saved,e
end
function R.stop()if timer then C.cancel(timer);timer=nil end;if waiting then waiting.complete(true,{audio_skipped=true});waiting=nil end;if playback then C.call('/dev/speaker','stop',{playback_id=playback})end end
function R.close()R.stop();if timeoutTimer then C.cancel(timeoutTimer);timeoutTimer=nil end;if playbackToken then notes.off(playbackToken);playbackToken=nil end;if timer then C.cancel(timer);timer=nil end;ack=nil;playback=nil;media=nil;waiting=nil end
function R.play(audio,complete)
 if R.mode()~='speech' or not audio then return end
 if playback then if waiting then waiting.complete(true,{audio_skipped=true})end;waiting={audio=audio,complete=complete};if timer then C.cancel(timer);timer=nil end;C.call('/dev/speaker','stop',{playback_id=playback});return false end
 if type(audio)~='table'or type(audio.packet_bytes)~='number'or audio.format~='wishos-audio/1'or audio.encoding~='ima-adpcm-block'or audio.channels~=1 or not audio.packet_bytes or audio.packet_bytes>1024 or audio.packet_bytes<12 then if notify then notify('unavailable','audio_format')end;return end
 local file,e=C.call('/fs','stat',{file=audio.file});if not file then if notify then notify('unavailable',e)end;return end
 local request,token=notes.request('/dev/speaker','start',{sample_rate_hz=audio.sample_rate_hz,channels=1,encoding=audio.encoding,buffer_blocks=16},function(e)if e.data.playback_id==playback then if e.event=='completed'then done(pendingError)elseif e.data.error=='cancelled'then done(pendingError)else done(pendingError or e.data.error)end end end);if not request.ok then if notify then notify('unavailable',request.error)end;return end;local started=request.result;playbackToken=token;pendingError=nil
 playback=started.playback_id;media=audio;offset=0;sequence=0;finished=false;ack=complete
 local function pump()
  if not playback or finished then return end
  for _=1,4 do
   local block,e=C.call('/fs','read',{file=media.file,offset=tostring(offset),length=media.packet_bytes})
   if not block then if e=='bus_busy'or e=='resource_busy'then return end;pendingError=e;R.stop();return end
   if block.hex==''then local result,e=C.call('/dev/speaker','finish',{playback_id=playback});if result then finished=true else pendingError=e;R.stop()end;return end
   local accepted,e=C.call('/dev/speaker','write',{playback_id=playback,sequence=sequence,hex=block.hex})
   if not accepted then if e=='audio_would_block'then return end;pendingError=e;R.stop();return end
   offset=tonumber(block.next_offset);sequence=sequence+1
  end
 end
 timer=C.timer(1,40,pump);local samples=tonumber(audio.samples)or math.ceil(tonumber(file.size)/audio.packet_bytes)*640;timeoutTimer=C.timer(C.config.reply_timeout_ms or math.ceil(samples/audio.sample_rate_hz*1000)+5000,0,function()pendingError='audio_timeout';R.stop()end);return false
end
function R.beep()
 local r,e=C.call('/dev/speaker','start',{sample_rate_hz=16000,channels=1,encoding='ima-adpcm-block',buffer_blocks=4});if not r then return false,e end
 for i=0,3 do
  local block,err=C.call('/fs','read',{file=notes.resolve('../brand/ready.adpcm'),offset=tostring(i*331),length=331})
  if not block then C.call('/dev/speaker','stop',{playback_id=r.playback_id});return false,err end
  local sent,err=C.call('/dev/speaker','write',{playback_id=r.playback_id,sequence=i,hex=block.hex});if not sent then C.call('/dev/speaker','stop',{playback_id=r.playback_id});return false,err end
 end
 local done,err=C.call('/dev/speaker','finish',{playback_id=r.playback_id});return done~=nil,err
end
return R
