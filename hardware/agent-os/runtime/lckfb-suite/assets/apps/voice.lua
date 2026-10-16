local C=notes.require('../suite/common.lua');local V=notes.require('../suite/voice.lua');local animation,frame,phase;local answer,answerPage,answerVisible
local function stop_animation() C.cancel(animation);animation=nil end
local function animate(nextPhase)
 stop_animation();phase=nextPhase;frame=0
 animation=C.timer(1,C.config.animation_interval_ms,function()
  local ui=C.call('/sys/ui');if not ui or ui.presentation.last_presented_revision~=ui.revision then return end
  C.update({activity=notes.resolve('../brand/'..phase..'-'..string.format('%02d',frame)..'.png')});frame=frame+1
  if phase=='done' and frame==12 then stop_animation();if answer and utf8.len(answer)>32 then C.show_answer() end else frame=frame%12 end
 end)
end
local function part(text,first,count) local a=utf8.offset(text,first) or (#text+1);local b=utf8.offset(text,first+count) or (#text+1);return text:sub(a,b-1) end
function C.show_answer()
 answerVisible=true;local pages=math.max(1,math.ceil(utf8.len(answer)/64));C.open('answer',{answer=part(answer,(answerPage-1)*64+1,64),page=tostring(answerPage)..' / '..tostring(pages)})
 C.action('previous',function() if answerPage>1 then answerPage=answerPage-1;C.show_answer() end end)
 C.action('next',function() if answerPage<pages then answerPage=answerPage+1;C.show_answer() end end)
end
local function show(state,detail)
 if state=='recording' then stop_animation();C.update({message=C.t('正在聆听','Listening'),detail=C.t('短按用户键或点发送结束','Press the user key or Send'),record_label=C.t('结束并发送','Send recording'),activity=notes.resolve('../icons/voice.png')});C.beep()
 elseif state=='processing' then C.update({message=C.t('交给 Agent 处理','Your Agent is working'),detail=C.t('可回到桌面继续使用','You can return Home'),record_label=C.t('继续说话','Say more')});animate('busy')
 elseif state=='answer' then answer=detail or '';answerPage=1;if V.active() then return end;C.update({message=part(answer,1,32),detail=C.t('短按继续说话 · 长按2秒回桌面','Press to speak · Hold 2s for Home'),record_label=C.t('开始录音','Record')});animate('done')
 elseif state=='completed' then if phase~='done' then stop_animation() end
 elseif state=='failed' then stop_animation();C.update({message=C.t('请求未完成','Request failed'),detail=detail or '',record_label=C.t('重新录音','Try again'),activity=notes.resolve('../brand/logo.png')}) end
end
local function toggle()
 if V.active() then V.finish();return end
 local link=C.call('/services/link','read');if not link or not link.connected then show('failed',C.t('先在 WishOS Hub 添加并配对本设备','Pair this device in WishOS Hub first'));return end
 local shown=C.call('/sys/ui','read',{view_id=C.view});if answerVisible or not shown or shown.path~=C.path('voice') or shown.presentation.active_view~=C.view then answerVisible=false;C.open('voice') end
 V.start()
end
function on_start()
 C.init();C.open('voice');V.init(C.config,show);C.action('record',toggle);C.button(toggle)
 local a=C.call('/sys/apps','read');if a and a.arguments and a.arguments.record then toggle() end
end
function on_stop() stop_animation();V.stop() end
