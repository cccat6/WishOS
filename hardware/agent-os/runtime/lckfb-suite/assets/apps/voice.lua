local C=notes.require('../suite/common.lua');local V=notes.require('../suite/voice.lua');local animation,frame,phase;local answer,answerPage,answerVisible
local function stop_animation() C.cancel(animation);animation=nil end
local function animate(nextPhase)
 stop_animation();phase=nextPhase;frame=0
 animation=C.timer(1,C.config.animation_interval_ms,function()
  local ui=C.call('/sys/ui');if not ui or ui.presentation.last_presented_revision~=ui.revision then return end
  local shown=phase=='done' and ({4,7,11})[frame+1] or frame;C.update({activity=notes.resolve('../brand/'..phase..'-'..string.format('%02d',shown)..'.png')});frame=frame+1
  if phase=='done' and frame==3 then stop_animation() else frame=frame%12 end
 end)
end
local function part(text,first,count) local a=utf8.offset(text,first) or (#text+1);local b=utf8.offset(text,first+count) or (#text+1);return text:sub(a,b-1) end
function C.show_answer()
 answerVisible=true;local pages=math.max(1,math.ceil(utf8.len(answer)/64));local hidden={};if answerPage==1 then hidden[#hidden+1]='previous' end;if answerPage==pages then hidden[#hidden+1]='next' end;C.open('answer',{answer=part(answer,(answerPage-1)*64+1,64),page=tostring(answerPage)..' / '..tostring(pages)},hidden)
 C.action('previous',function() if answerPage>1 then answerPage=answerPage-1;C.show_answer() end end)
 C.action('next',function() if answerPage<pages then answerPage=answerPage+1;C.show_answer() end end)
end
local function failure_text(code)
 local messages={audio_loss_exceeded={'录音传输丢失过多，请再说一次','Too much audio was lost. Please try again.'},recording_not_found={'录音缓存已过期，请再说一次','Recording expired. Please try again.'},audio_memory_budget={'暂时资源不足，请稍后重试','Resources are busy. Please try again.'},bus_busy={'设备正忙，请稍后重试','Device busy. Please try again.'},capture_stopping={'设备正在准备，请稍后重试','Device is preparing. Please retry shortly.'}}
 local text=messages[code or ''];return text and C.t(text[1],text[2]) or code or ''
end
local function show(state,detail)
 if state=='starting' then stop_animation();C.update({message=C.t('正在启动麦克风','Starting microphone'),detail=C.t('就绪后会有提示音','A tone signals readiness'),record_label=C.t('结束录音','Stop recording'),activity=notes.resolve('../brand/microphone.png')})
 elseif state=='recording' then stop_animation();C.update({message=C.t('正在聆听','Listening'),detail=C.t('短按用户键或点发送结束','Press the user key or Send'),record_label=C.t('结束并发送','Send recording'),activity=notes.resolve('../brand/microphone.png')});local cue=C.beep();if not cue then C.update({detail=C.t('麦克风已就绪，可以说话','Microphone ready; speak now')}) end
 elseif state=='processing' then C.update({message=C.t('交给 Agent 处理','Your Agent is working'),detail=C.t('文字回复请留在此页\n短按可继续补充','Stay here for text replies\nPress to add more'),record_label=C.t('继续说话','Say more')});animate('busy')
 elseif state=='answer' then answer=detail or '';answerPage=1;if V.active() then return end;C.show_answer();animate('done')
 elseif state=='completed' then if phase~='done' then stop_animation() end
 elseif state=='failed' then stop_animation();C.update({message=C.t('请求未完成','Request failed'),detail=failure_text(detail),record_label=C.t('重新录音','Try again'),activity=notes.resolve('../brand/microphone.png')}) end
end
local function toggle()
 if V.active() then V.finish();return end
 local link=C.call('/services/link','read');if not link or not link.connected then show('failed',C.t('先在 WishOS Hub 添加并配对本设备','Pair this device in WishOS Hub first'));return end
 local shown=C.call('/sys/ui','read',{view_id=C.view});if answerVisible or not shown or shown.path~=C.path('voice') or shown.presentation.active_view~=C.view then answerVisible=false;C.open('voice') end
 V.start()
end
function on_start()
 C.init();C.open('voice');V.init(C.config,show);C.action('record',toggle);C.button(toggle)
 C.action('home',function() if answerVisible then stop_animation();answerVisible=false;C.open('voice',{message=part(answer,1,24),detail=C.t('点回复查看全文；短按继续说话','Tap the reply to read; press to speak')}) else C.home() end end)
 C.action('message',function() if answer and not V.active() then stop_animation();C.show_answer() end end)
 local a=C.call('/sys/apps','read');if a and a.arguments and a.arguments.record then toggle() end
end
function on_stop() stop_animation();V.stop() end
