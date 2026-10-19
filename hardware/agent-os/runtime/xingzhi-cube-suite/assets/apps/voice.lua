local C=notes.require('../suite/common.lua');local ai=notes.require('/system/lib/ai.lua');local V=notes.require('../suite/voice.lua');local S=notes.require('../suite/sessions.lua');local A=notes.require('../suite/activity.lua');local R=notes.require('../suite/reply.lua')
local text='';local page=1;local mode='voice';local rows={};local selected=1;local choices={};local nextCursor;local cursors={false};local sessionPage=1;local wantList=false;local operation=1;local heldRequest;local wantRecording=false;local voiceEntry
local function excerpt(t,p)local a=utf8.offset(t,(p-1)*24+1)or #t+1;local b=utf8.offset(t,p*24+1)or #t+1;return t:sub(a,b-1)end
local function same(a,b)return a and b and a.runtime_id==b.runtime_id and a.session_id==b.session_id and a.session_scope==b.session_scope end
local function menu()
 local list={{title=C.t('会话','Chats'),action='sessions'}}
 if R.available then list[#list+1]={title=C.t('切换回复','Switch reply'),action='output'}end
 if utf8.len(text)>24 then list[#list+1]={title=C.t('下一页','Next'),action='next'};list[#list+1]={title=C.t('上一页','Previous'),action='previous'}end
 list[#list+1]={title=C.t('桌面','Home'),action='home'};return list
end
local function operation_values()
 local list=menu();operation=math.min(operation,#list);local names={};for _,item in ipairs(list)do names[#names+1]=item.title end
 local v=C.choice_preview(names,operation);v.detail=names[operation];v.output_label=R.label()..C.t('回复',' reply');return v
end
local function voice_view()
 mode='voice';operation=1;local v=operation_values();v.title=C.t('对话','Talk');v.session_title=S.title();v.message=#text>0 and excerpt(text,page)or C.t('按住说话，松开发送','Hold to speak; release to send');v.hint=C.t('单击切换 · 双击确认\n三击回桌面 · 长按说话','Click: select · Double: open\nTriple: Home · Hold: talk');C.open(v,'voice',R.available and {}or {'output_label'})
end
local function session_view(newPage)
 local reuse=mode=='sessions'and C.view and not newPage;mode='sessions';choices={}
 for _,entry in ipairs(rows)do choices[#choices+1]={title=entry.title or entry.reference.session_id,reference=entry.reference}end
 if nextCursor then choices[#choices+1]={title=C.t('下一页','Next'),action='next'}end
 if sessionPage>1 then choices[#choices+1]={title=C.t('上一页','Previous'),action='previous'}end
 if S.supports('create')then choices[#choices+1]={title=C.t('新建会话','New chat'),action='create'}end
 choices[#choices+1]={title=C.t('返回对话','Back'),action='back'};selected=math.min(selected,#choices)
 local names={};for _,entry in ipairs(choices)do names[#names+1]=entry.title end;local v=C.choice_preview(names,selected);v.title=C.t('会话','Chats');v.message=excerpt(names[selected],1);v.detail=tostring(selected)..' / '..#choices;v.session_title=C.t('选择已有对话，或新建','Choose or create a conversation');v.hint=C.t('单击切换 · 双击确认\n三击回桌面 · 长按说话','Click: select · Double: open\nTriple: Home · Hold: talk');if reuse then C.update(v)else C.open(v,'voice')end
end
local function show(state,data,metadata)
 if state=='session'then S.accept(data);return end
 if metadata and metadata.session and not same(metadata.session,S.reference())then return end
 if state=='answer'then A.stop();text=data or '';page=1;if mode=='sessions'then return end;voice_view();if metadata and metadata.audio_error then C.update({session_title=C.t('本次使用文字回复','Text reply this time')})end;return metadata and R.play(metadata.audio,metadata.acknowledge)
 elseif state=='recording'then A.stop();C.update({message=C.t('正在聆听，松开发送','Listening; release to send'),session_title=S.title(),activity=notes.resolve('../brand/microphone.png')})
 elseif state=='processing'then A.start('busy');C.update({message=C.t('Agent 正在处理','Agent is working'),session_title=C.t('可以按住继续补充','Hold to add more')})
 elseif state=='progress'then if V.active()or mode=='sessions'then return end;local stages={received={'已接收','Received'},receiving={'接收录音','Receiving audio'},transcribing={'识别语音','Transcribing'},executing={'正在执行','Working'},preparing={'整理结果','Preparing reply'},transferring={'传送结果','Delivering'}};local title=stages[data.stage]or stages.executing;C.update({session_title=C.t(title[1],title[2]),message=excerpt(data.text and data.text~=''and data.text or C.t(title[1],title[2]),1)})
 elseif state=='starting'then A.stop();C.update({message=C.t('准备麦克风…','Preparing microphone…'),activity=notes.resolve('../brand/microphone.png')})
 elseif state=='failed'then A.stop();C.update({message=C.t('请求未完成，请再试一次','Request failed; please retry'),session_title=C.t('可以回桌面检查连接','Check connection from Home'),activity=notes.resolve('../brand/microphone.png')})end
end
local function held(id)local state=C.call('/sys/apps')or {};return id and state.voice_input_held and state.voice_request_id==id end
local function record()
 local link=C.call('/services/link')or {};if not link.connected then show('failed');return end
 if not S.reference()then wantRecording=true;C.update({message=C.t('正在准备对话…','Preparing conversation…')});S.ensure();return end
 R.stop();V.start(R.context(S.reference()))
end
local function list_sessions()
 if S.supports('list')then S.list(cursors[sessionPage]or nil)else rows=S.reference()and {{reference=S.reference(),title=S.title()}}or {};selected=1;session_view(true)end
end
local function next_item()
 if mode=='sessions'then if not S.busy then selected=selected%#choices+1;session_view()end;return end
 local list=menu();operation=operation%#list+1;C.update(operation_values())
end
local function confirm()
 if mode=='sessions'then if S.busy then return end;local item=choices[selected];if item.reference then if S.supports('select')then S.select(item.reference)end
  elseif item.action=='create'then S.create()elseif item.action=='next'then sessionPage=sessionPage+1;cursors[sessionPage]=nextCursor;S.list(nextCursor)
  elseif item.action=='previous'then sessionPage=sessionPage-1;S.list(cursors[sessionPage]or nil)else voice_view()end;return end
 local action=menu()[operation].action
 if action=='sessions'then wantList=true;cursors={false};sessionPage=1;if not S.busy then S.status()end
 elseif action=='output'then if R.toggle()then C.update(operation_values())else C.update({session_title=C.t('回复设置未保存','Reply setting was not saved')})end
 elseif action=='home'then C.call('/sys/apps','home')
 else page=math.max(1,math.min(math.max(1,math.ceil(utf8.len(text)/24)),page+(action=='next'and 1 or -1)));C.update({message=excerpt(text,page)})end
end
function on_start()
 C.init();A.init(C);R.init(C,function(state)if state=='unavailable'then C.update({session_title=C.t('本次使用文字回复','Text reply this time')})end end);voice_view();V.init(C.config,show)
 S.init(function(state,data)
  if state=='failed'then wantList=false;wantRecording=false;show('failed')
  elseif state=='list'then rows=data.entries or {};nextCursor=data.next_cursor~=notes.null and data.next_cursor or nil;selected=1;session_view(true)
  elseif state=='selected'then text='';page=1;voice_view()
  elseif state=='ready'then if wantRecording then wantRecording=false;if held(heldRequest)then record()end elseif wantList then wantList=false;list_sessions()end
  elseif state=='idle'then if wantList then wantList=false;list_sessions()end end
 end)
 voiceEntry=ai.voice_input(function(input)heldRequest=input.request_id;record()end,function(input)if heldRequest==input.request_id then heldRequest=nil;wantRecording=false;V.finish()end end)
 notes.on('/dev/buttons','click',function(e)if V.active()then return end;C.click(e.data,next_item,confirm)end)
 local link=C.call('/services/link')or {};if link.connected then if S.reference()then S.status()else S.ensure()end end
end
function on_stop()if voiceEntry then voiceEntry.stop()end;A.stop();R.close();V.stop();S.stop()end
