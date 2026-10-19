-- Cloud session references are app preferences. History and memory stay in the
-- selected Harness; all control requests pass through the system AI broker.
local C=notes.require('../suite/common.lua');local S={operations={},busy=false};local notify;local pending;local prefix;local serial=0;local createAfterStatus=false;local ensuring=false
local function ref(input,value) for _,key in ipairs({'runtime_id','session_id','session_scope'}) do if value and value[key] then input[key]=value[key] end end end
function S.reference() return type(C.preferences.conversation)=='table' and C.preferences.conversation or nil end
function S.title() local title=type(S.session)=='table' and S.session.title;return title and title~='' and title or C.t('当前会话','Current conversation') end
function S.mode() return C.preferences.conversation_mode or 'fixed' end
function S.toggle_mode() return C.change_preferences({conversation_mode=S.mode()=='fixed' and 'selectable' or 'fixed'}) end
function S.supports(operation) for _,name in ipairs(S.operations) do if name==operation then return true end end return false end
function S.init(callback) notify=callback end
function S.request(operation,args)
 if S.busy then return nil,'session_control_busy' end
 local input={kind='session_control',operation=operation};for key,value in pairs(args or {}) do input[key]=value end
 S.busy=true;notify('loading',operation)
 local sent=notes.request('/services/ai','submit',{input=input,timeout_ms=C.config.session_timeout_ms},function(e)
  local state=C.call('/services/ai','read',{id=e.data.id});C.call('/services/ai','release',{id=e.data.id})
  if not pending or pending.id~=e.data.id then return end
  pending=nil;S.busy=false
  if e.event~='completed' then ensuring=false;notify('failed',state and state.error or C.t('会话请求未完成','Session request failed'))
  elseif ensuring then
   if S.reference() then ensuring=false;notify('ready',S.reference())
   elseif operation=='status' and S.supports('create') and not C.preferences.pending_creation then S.create()
   else ensuring=false;notify('failed',C.preferences.pending_creation and C.t('请核对上次会话创建结果','Check the previous session creation') or C.t('请在 Hub 连接可用的 Agent','Connect an available Agent in Hub')) end
  elseif createAfterStatus then createAfterStatus=false;S.create() else notify('idle',operation) end
 end)
 if not sent.ok then S.busy=false;notify('failed',sent.error);return nil,sent.error end
 pending={id=sent.result.id,key=sent.result.request_id,operation=operation};return sent.result
end
function S.ensure()
 if S.reference() then notify('ready',S.reference());return end
 ensuring=true;if not S.busy then return S.status() end
end
function S.status()
 local input={};ref(input,S.reference());if C.preferences.pending_creation then input.operation_id=C.preferences.pending_creation end
 return S.request('status',input)
end
function S.list(cursor) return S.request('list',{cursor=cursor or notes.null,limit=3}) end
function S.select(reference) local input={};ref(input,reference);return S.request('select',input) end
function S.messages(cursor) local input={cursor=cursor or notes.null,limit=1,text_max_bytes=256};ref(input,S.reference());return S.request('messages',input) end
function S.create()
 if not S.supports('create') then notify('failed',C.t('当前连接未提供新建会话','This connection cannot create sessions'));return end
 if not prefix then createAfterStatus=true;S.status();return end
 if not C.preferences.pending_creation then serial=serial+1;local saved,e=C.change_preferences({pending_creation=prefix..':'..serial});if not saved then notify('failed',e);return end end
 return S.request('create',{operation_id=C.preferences.pending_creation})
end
function S.accept(data)
 if data.format~='wishos-session-control/1' then return end
 if pending and data.request_id~=pending.key and data.operation~='select' then return end
 if not data.ok then notify('failed',data.error and data.error.code or 'session_control_failed');return end
 prefix=data.request_id or prefix
 if data.operation=='status' then
  S.operations=data.operations or {};S.session=data.session
  local creation=type(data.creation)=='table' and data.creation or nil
  if C.preferences.pending_creation then
   if creation and creation.state=='completed' and type(creation.selected_session)=='table' then local saved,e=C.change_preferences({conversation=creation.selected_session},{'pending_creation'});if not saved then notify('failed',e);return end;notify('selected',data)
   else notify('creation_pending',creation or {}) end
   return
  end
  local selected=data.selected_session;if not S.reference() and (not selected or selected==notes.null) then selected=data.default_session end
  if selected and selected~=notes.null and not S.reference() then local saved,e=C.change_preferences({conversation=selected});if not saved then notify('failed',e);return end end
  notify('status',data)
 elseif data.operation=='select' or data.operation=='create' then
  local saved,e=C.change_preferences({conversation=data.selected_session},{'pending_creation'});if not saved then notify('failed',e);return end;S.session=data.session;notify('selected',data)
 else notify(data.operation,data) end
end
function S.stop()
 if pending and pending.operation~='create' then
  C.call('/services/ai','cancel',{id=pending.id});local state=C.call('/services/ai','read',{id=pending.id})
  if state and (state.state=='completed' or state.state=='failed') then C.call('/services/ai','release',{id=pending.id}) end
 end
 pending=nil;S.busy=false;ensuring=false
end
return S
