local C=notes.require('../suite/common.lua');local root='/storage/user';local dir=root;local cursors={0};local index=1;local page;local mode=1;local detail=false;local detailFile;local detailPage=1;local scanTimer
local categories={{'全部类型','All types'},{'书籍文档','Documents'},{'图片','Images'},{'音频','Audio'}}
local function kind(f)
 if f.directory then return 0,C.t('文件夹','Folder') end
 local at,last=1;while true do local i=notes.find(f.name,'.',at);if not i then break end;last=i;at=i+1 end;local ext=(last and f.name:sub(last+1) or ''):lower()
 if ext=='txt' or ext=='epub' or ext=='pdf' or ext=='md' then return 2,C.t('书籍 / 文档','Book / document') end
 if ext=='png' or ext=='jpg' or ext=='jpeg' or ext=='bin' then return 3,C.t('图片','Image') end
 if ext=='wav' or ext=='mp3' or ext=='adpcm' or ext=='opus' then return 4,C.t('音频','Audio') end
 return 1,ext~='' and ext:upper() or C.t('文件','File')
end
local function size(n) n=tonumber(n) or 0;if n>=1048576 then return string.format('%.1f MB',n/1048576) elseif n>=1024 then return string.format('%.1f KB',n/1024) end return tostring(n)..' B' end
local function short_name(name)
 local chars={};local units=0;for _,cp in utf8.codes(name) do local width=cp<128 and 1 or 2;chars[#chars+1]={utf8.char(cp),width};units=units+width end
 if units<=34 then return name end
 local prefix,suffix={},{};units=0
 for _,c in ipairs(chars) do if units+c[2]>23 then break end;prefix[#prefix+1]=c[1];units=units+c[2] end
 units=0;for i=#chars,1,-1 do local c=chars[i];if units+c[2]>8 then break end;table.insert(suffix,1,c[1]);units=units+c[2] end
 return table.concat(prefix)..'…'..table.concat(suffix)
end
local function show_detail()
 local f=detailFile;local text=f.name..'\n'..dir;local pages=math.max(1,math.ceil(utf8.len(text)/42));local first=utf8.offset(text,(detailPage-1)*42+1) or #text+1;local last=utf8.offset(text,detailPage*42+1) or #text+1;local _,label=kind(f)
 local hidden={};if detailPage==1 then hidden[#hidden+1]='previous' end;if detailPage==pages then hidden[#hidden+1]='next' end;C.open('file-info',{filename=text:sub(first,last-1),fileinfo=label..' · '..size(f.size),page=tostring(detailPage)..' / '..tostring(pages)},hidden)
end
local function render()
 local v={directory=(dir==root and '/' or dir:sub(#root+1)),filter=categories[mode][C.en() and 2 or 1]}
 for i=0,2 do local f=page.entries[i+1];local _,label=kind(f or {name=''})
  v['file'..i..'_title']=f and short_name(f.name) or ''
  v['file'..i..'_meta']=f and (label..(f.directory and '  /' or '  ·  '..size(f.size))) or ''
 end
 v.empty=#page.entries==0 and C.t('此目录没有符合类型的文件','No matching files') or '';local hidden={};if #page.entries>0 then hidden[#hidden+1]='empty' end;if index==1 then hidden[#hidden+1]='previous' end;if not page.more then hidden[#hidden+1]='next' end;for i=#page.entries,2 do hidden[#hidden+1]='file'..i end;C.open('files',v,hidden);detail=false
end
local function show()
 C.cancel(scanTimer);page={entries={},more=true,next_cursor=cursors[index]};local generation=page
 local function scan()
  if page~=generation then return end
  local r,e=C.call('/fs','list',{file=dir,cursor=page.next_cursor,limit=3-#page.entries})
  if not r then C.update({directory=C.t('读取失败：','Read failed: ')..(e or '')});return end
  for _,f in ipairs(r.entries) do local category=kind(f);if mode==1 or category==0 or category==mode then page.entries[#page.entries+1]=f end end
  page.next_cursor=r.next_cursor;page.more=r.more
  if #page.entries<3 and page.more then scanTimer=C.timer(1,0,scan) else render() end
 end
 scan()
end
local function back()
 if detail then render();return end
 if dir==root then C.home();return end
 local at,last=1,1;while true do local i=notes.find(dir,'/',at);if not i then break end;last=i;at=i+1 end;dir=dir:sub(1,last-1);cursors={0};index=1;show()
end
function on_start()
 C.init();C.open('files',{directory='/',empty=C.t('正在挂载 SD 卡','Mounting SD card')},{'file0','file1','file2','previous','next','filter'});C.prepare_sd(function(s,e) if s and s.mounted then show() else C.update({empty=e or C.t('SD 卡不可用','SD card unavailable')}) end end)
 C.action('home',back)
 for i=0,2 do C.action('file'..i,function()
  local f=page and page.entries[i+1];if not f then return end
  if f.directory then dir=dir..'/'..f.name;cursors={0};index=1;show()
  else detail=true;detailFile=f;detailPage=1;show_detail() end
 end) end
 C.action('previous',function() if detail then if detailPage>1 then detailPage=detailPage-1;show_detail() end;return end;if index>1 then index=index-1;show() end end)
 C.action('next',function() if detail then if detailPage*42<utf8.len(detailFile.name..'\n'..dir) then detailPage=detailPage+1;show_detail() end;return end;if page and page.more then cursors[index+1]=page.next_cursor;index=index+1;show() end end)
 C.action('refresh',function() cursors={0};index=1;show() end)
 C.action('filter',function() mode=mode%#categories+1;cursors={0};index=1;show() end)
 C.button(function() C.launch('voice',{record=true}) end)
end
