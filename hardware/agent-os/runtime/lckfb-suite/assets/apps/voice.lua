local ai=notes.require('/system/lib/ai.lua');local voiceInput;local C=notes.require('../suite/common.lua');local V=notes.require('../suite/voice.lua');local P=notes.require('../suite/conversation.lua');local R=notes.require('../suite/reply.lua');local wanted=false;local touchTimer,touchOwn,buttonOwn;local origin;local phase;local answer,answerPage,answerVisible,answerReference
local A=notes.require('../suite/activity.lua')
local function output_values()return {output_label=R.label()}end
local function voice_page(values)C.action('message',answer and function()if not V.active()then A.stop();C.show_answer()end end or nil);local v=values or {};v.output_label=R.label();C.open('voice',v,R.available and {}or {'output'})end
local function stop_animation()A.stop()end
local function animate(nextPhase)phase=nextPhase;A.start(phase)end
local function part(text,first,count) local a=utf8.offset(text,first) or (#text+1);local b=utf8.offset(text,first+count) or (#text+1);return text:sub(a,b-1) end
function C.show_answer()
 answerVisible=true;local pages=math.max(1,math.ceil(utf8.len(answer)/60));local hidden={};if not R.available then hidden[#hidden+1]='output'end;if answerPage==1 then hidden[#hidden+1]='previous' end;if answerPage==pages then hidden[#hidden+1]='next' end;C.open('answer',{answer=part(answer,(answerPage-1)*60+1,60),page=tostring(answerPage)..' / '..tostring(pages),output_label=R.label()},hidden)
 C.action('previous',function() if answerPage>1 then answerPage=answerPage-1;C.show_answer() end end)
 C.action('next',function() if answerPage<pages then answerPage=answerPage+1;C.show_answer() end end)
end
local function show(state,detail,metadata)
 if state=='session' then P.accept(detail)
 elseif state=='starting' then stop_animation();C.update({message=C.t('正在启动麦克风','Starting microphone'),detail=C.t('就绪后会有提示音','A tone signals readiness'),record_label=C.t('请保持按住','Keep holding'),activity=notes.resolve('../brand/microphone.png')})
 elseif state=='recording' then stop_animation();C.update({message=C.t('正在聆听','Listening'),detail=C.t('松开按键或屏幕即可发送','Release the button or screen to send'),record_label=C.t('松开发送','Release to send'),activity=notes.resolve('../brand/microphone.png')});local cue=R.beep();if not cue then C.update({detail=C.t('麦克风已就绪，可以说话','Microphone ready; speak now')}) end
 elseif state=='processing' then C.update({message=C.t('交给 Agent 处理','Your Agent is working'),detail='',record_label=C.t('按住补充','Hold to add more')});animate('busy')
 elseif state=='progress' then
  if V.active() or P.active() or answerVisible or metadata and metadata.session and not P.matches(metadata.session) then return end
  local titles={received={'请求已接收','Request received'},receiving={'正在接收录音','Receiving audio'},transcribing={'正在识别语音','Transcribing'},executing={'Agent 正在执行','Agent is working'},preparing={'正在整理结果','Preparing reply'},transferring={'正在传送结果','Delivering result'}}
  local title=titles[detail.stage] or titles.executing;C.update({message=C.t(title[1],title[2]),detail=part(detail.text or '',1,40)})
 elseif state=='answer' then if metadata and metadata.session and not P.matches(metadata.session) then if not P.active() then C.update({detail=C.t('其他会话已有新回复','Another conversation has a reply')}) end;return end;answerReference=metadata and metadata.session;answer=detail or '';answerPage=1;if V.active() or P.active() then return end;stop_animation();C.show_answer();animate('done');if metadata and metadata.audio_error then C.update({page=C.t('语音暂不可用','Voice unavailable')})end;return metadata and R.play(metadata.audio,metadata.acknowledge)
 elseif state=='completed' then if phase~='done' then stop_animation() end
 elseif state=='failed' then stop_animation();C.update({message=C.t('请求未完成','Request failed'),detail=V.failure_text(C,detail),record_label=C.t('按住重试','Hold to retry'),activity=notes.resolve('../brand/microphone.png')}) end
end
local function toggle()
 if V.active()then return end
 local link=C.call('/services/link','read');if not link or not link.connected then show('failed',C.t('先在 WishOS Hub 添加并配对本设备','Pair this device in WishOS Hub first'));return end
 local shown=C.call('/sys/ui','read',{view_id=C.view});if answerVisible or not shown or shown.path~=C.path('voice') or shown.presentation.active_view~=C.view then answerVisible=false;voice_page() end
 local reference=P.reference();if not reference then wanted=true;C.update({detail=C.t('正在准备对话…','Preparing conversation…')});P.ensure();return end
 R.stop();V.start(R.context(reference))
end
function on_start()
 C.init();A.init(C);R.init(C,function(state)if state=='unavailable'then C.update({page=C.t('语音暂不可用，已显示文字','Voice unavailable; text kept')})end end);voice_page();V.init(C.config,show)
 local context=C.call('/sys/apps','context') or {};local apps=C.call('/sys/apps','read') or {};local source=context.launch_origin
 if source and source.application_id and source.application_id~=apps.launcher and source.application_id~=apps.voice then origin=source.application_id;local app=C.call('/sys/apps','inspect',{id=origin});local name=app and app.application.title or origin;C.update({detail=C.t('来自应用：','From application: ')..part(name,1,18)..C.t('\n可对话，或描述如何修改','\nTalk, or describe a change')}) end
 P.init(function() answerVisible=false;if answerReference and not P.matches(answerReference) then answer=nil end;voice_page({session_title=P.title(),message=answer and part(answer,1,24) or C.t('说点什么，或让我制作应用','Talk, or ask for a tool')}) end,function(reference) if wanted and reference then wanted=false;toggle() end end)
 C.action('sessions',function() wanted=false;if V.active() then V.finish() end;stop_animation();P.open() end)
 C.action('record',function()R.stop();answerVisible=false;voice_page({session_title=P.title()})end);C.hold_action('speak',function()if not V.active()then touchTimer=C.timer(C.config.touch_voice_hold_ms or 250,0,function()touchTimer=nil;touchOwn=true;wanted=true;toggle()end)end end,function()C.cancel(touchTimer);touchTimer=nil;if touchOwn then touchOwn=false;wanted=false;V.finish()end end,function()C.cancel(touchTimer);touchTimer=nil;if touchOwn then touchOwn=false;wanted=false;if V.active()then V.cancel()end;C.update({message=C.t('已取消录音','Recording cancelled'),detail='',record_label=C.t('按住说话','Hold to speak')})end end);C.action('output',function()local saved=R.toggle();if saved then C.update(output_values())else C.update({detail=C.t('回复设置未保存，请重试','Reply setting was not saved; retry')})end end)
 C.action('home',function() if P.back() then return end;if answerVisible then stop_animation();answerVisible=false;voice_page({message=part(answer,1,24),detail=C.t('点回复查看全文；按住可继续说话','Tap the reply to read; hold to speak')}) elseif origin then C.launch(origin) else C.home() end end)
 voiceInput=ai.voice_input(function()if not V.active()then buttonOwn=true;wanted=true;toggle()end end,function()if buttonOwn then buttonOwn=false;wanted=false;V.finish()end end)
end
function on_stop() if voiceInput then voiceInput.stop() end;stop_animation();C.cancel(touchTimer);R.close();V.stop();P.stop() end
