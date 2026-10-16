local C=notes.require('../suite/common.lua');local dir='/storage/user';local cursor=0;local page
local function show()
 local r,e=C.call('/fs','list',{file=dir,cursor=cursor,limit=4});if not r then C.update({directory=C.t('读取失败：','Read failed: ')..(e or '')});return end;page=r;local v={directory=dir};for i=0,3 do local f=page and page.entries[i+1];v['file'..i..'_title']=f and ((f.directory and '> ' or '')..f.name) or '' end
 C.open('files',v)
 for i=0,3 do C.action('file'..i,function() local f=page and page.entries[i+1];if f and f.directory then dir=dir..'/'..f.name;cursor=0;show() elseif f then C.update({directory=f.name..'  '..tostring(f.size or '')..' B'}) end end) end
end
function on_start()
 C.init();C.open('files',{directory=C.t('正在挂载 SD 卡','Mounting SD card')});C.prepare_sd(function(s,e) if s and s.mounted then show() else C.update({directory=e or C.t('SD 卡不可用','SD card unavailable')}) end end)
 C.action('parent',function() if dir~='/storage/user' then local at,last=1,1;while true do local i=notes.find(dir,'/',at);if not i then break end;last=i;at=i+1 end;dir=dir:sub(1,last-1) end;cursor=0;show() end)
 C.action('next',function() cursor=page and page.more and page.next_cursor or 0;show() end)
 C.action('nfs',function() local r,e=C.call('/services/nfs','start',{provider='user',export_path='/sd'});C.update({directory=r and C.t('NFS 已开启：设备地址:/sd','NFS enabled: device-address:/sd') or e or ''}) end)
 C.button(function() C.launch('voice',{record=true}) end)
end
