-- Replaceable session browser. It retains references/cursors and small visible
-- excerpts, never a local conversation database or a second AI transport.
local C=notes.require('../suite/common.lua');local S=notes.require('../suite/sessions.lua');local P={};local backToVoice,ready,screen,rows,notice,nextCursor;local cursors,index={false},1;local wantsList=false;local historyCursors,historyIndex={false},1;local historyText,historyPart,historyNext='',1,nil
local function part(text,first,count) text=text or '';local a=utf8.offset(text,first) or #text+1;local b=utf8.offset(text,first+count) or #text+1;return text:sub(a,b-1) end
local function same(a,b) return a and b and a.session_id==b.session_id and a.runtime_id==b.runtime_id and a.session_scope==b.session_scope end
local function draw()
 local values={mode_label=S.mode()=='fixed' and C.t('固定机器人','Single bot') or C.t('多会话','Sessions'),empty=notice or C.t('没有更多会话','No conversations'),new_label=C.preferences.pending_creation and C.t('核对创建结果','Check creation') or C.t('新建会话','New chat')};local hidden={}
 for i=0,2 do local entry=rows and rows[i+1];values['session'..i..'_title']=entry and part(entry.title and entry.title~='' and entry.title or C.t('未命名 · ','Untitled · ')..part(entry.reference.session_id,math.max(1,utf8.len(entry.reference.session_id)-5),6),1,40) or '';values['session'..i..'_meta']=entry and (same(entry.reference,S.reference()) and C.t('当前会话','Current conversation') or part(entry.summary or '',1,18)) or '';if not entry then hidden[#hidden+1]='session'..i end end
 if rows and #rows>0 then hidden[#hidden+1]='empty' end;if index==1 then hidden[#hidden+1]='session_previous' end;if not nextCursor then hidden[#hidden+1]='session_next' end
 if not S.supports('list') and not S.supports('create') then hidden[#hidden+1]='mode' end
 if not S.supports('create') or (S.mode()=='fixed' and S.reference() and not C.preferences.pending_creation) then hidden[#hidden+1]='new' end
 if not S.reference() or not S.supports('messages') then hidden[#hidden+1]='history' end
 C.open('sessions',values,hidden)
end
local function draw_history()
 local pieces=math.max(1,math.ceil(utf8.len(historyText)/64));local hidden={};if historyIndex==1 and historyPart==1 then hidden[#hidden+1]='session_previous' end;if historyPart==pieces and not historyNext then hidden[#hidden+1]='session_next' end
 C.open('conversation-history',{answer=part(historyText,(historyPart-1)*64+1,64),page=tostring(historyIndex)..' · '..historyPart..' / '..pieces},hidden)
end
function P.reference() return S.reference() end
function P.matches(reference) return same(reference,S.reference()) end
function P.title() return S.title() end
function P.busy() return S.busy end
function P.accept(data) S.accept(data) end
function P.open()
 screen='sessions';rows={};notice=C.t('正在读取云端会话…','Loading cloud conversations…');cursors={false};index=1;nextCursor=nil;wantsList=true;draw();if not S.busy then wantsList=false;if S.supports('list') then S.list() else rows=S.reference() and {{reference=S.reference(),title=S.title()}} or {};draw() end end
end
function P.back()
 if screen=='history' then screen='sessions';draw();return true end
 if screen=='creation' then screen='sessions';draw();return true end
 if screen=='sessions' then screen=nil;backToVoice();return true end
 return false
end
function P.active() return screen~=nil end
function P.init(returnVoice,onReady)
 backToVoice=returnVoice;ready=onReady
 S.init(function(state,data)
  if state=='loading' then if screen=='sessions' then C.update({mode_label=C.t('读取中…','Loading…')}) elseif screen=='history' then C.update({page=C.t('读取中…','Loading…')}) else C.update({session_title=C.t('正在读取云端会话…','Loading cloud session…')}) end;return end
  if state=='failed' then notice=C.t('会话请求失败：','Session request failed: ')..tostring(data);if screen=='sessions' then rows={};draw() else C.update({detail=notice}) end;return end
  if state=='status' then if screen=='sessions' then wantsList=S.supports('list');if not wantsList then rows=S.reference() and {{reference=S.reference(),title=S.title()}} or {};draw() end end
  elseif state=='list' then rows=data.entries or {};nextCursor=data.next_cursor~=notes.null and data.next_cursor or nil;notice=nil;if screen=='sessions' then draw() end
  elseif state=='messages' then local entry=data.entries and data.entries[1];historyText=entry and ((entry.role=='user' and C.t('我：','Me: ') or 'Agent: ')..entry.text..(entry.truncated and C.t('\n（节选）','\n(excerpt)') or '')) or C.t('本页没有公开消息','No public messages on this page');historyPart=1;historyNext=data.next_cursor~=notes.null and data.next_cursor or nil;if screen=='history' then draw_history() end
  elseif state=='creation_pending' then screen='creation';C.open('pending-session')
  elseif state=='selected' then screen=nil;backToVoice()
  elseif state=='idle' then if wantsList and screen=='sessions' and S.supports('list') then wantsList=false;S.list(cursors[index] or nil) end;if ready then ready(S.reference()) end end
  if not screen then C.update({session_title=S.title()}) end
 end)
 C.action('mode',function() if S.busy then return end;local r,e=S.toggle_mode();if r then draw() else notice=e;draw() end end)
 C.action('check_creation',function() if not S.busy then S.status() end end)
 C.action('discard_creation',function() if S.busy then return end;local saved,e=C.change_preferences({},{'pending_creation'});if saved then P.open() else notice=e;screen='sessions';draw() end end)
 C.action('new',function() if S.busy then return end;if C.preferences.pending_creation then S.status() else S.create() end end)
 C.action('history',function() if S.busy or not S.supports('messages') then return end;screen='history';historyCursors={false};historyIndex=1;historyText=C.t('正在读取云端记录…','Loading cloud excerpts…');historyPart=1;historyNext=nil;draw_history();S.messages() end)
 for i=0,2 do C.action('session'..i,function() if S.busy then return end;local entry=rows and rows[i+1];if entry then if same(entry.reference,S.reference()) then screen=nil;backToVoice() elseif S.supports('select') then S.select(entry.reference) end end end) end
 C.action('session_previous',function()
  if S.busy then return end
  if screen=='history' then if historyPart>1 then historyPart=historyPart-1;draw_history() elseif historyIndex>1 then historyIndex=historyIndex-1;S.messages(historyCursors[historyIndex] or nil) end
  elseif screen=='sessions' and index>1 then index=index-1;S.list(cursors[index] or nil) end
 end)
 C.action('session_next',function()
  if S.busy then return end
  if screen=='history' then if historyPart*64<utf8.len(historyText) then historyPart=historyPart+1;draw_history() elseif historyNext then historyIndex=historyIndex+1;historyCursors[historyIndex]=historyNext;S.messages(historyNext) end
  elseif screen=='sessions' and nextCursor then index=index+1;cursors[index]=nextCursor;S.list(nextCursor) end
 end)
 S.status()
end
function P.stop() S.stop() end
return P
