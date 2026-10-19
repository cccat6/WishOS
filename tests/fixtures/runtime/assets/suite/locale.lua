-- App-owned language preference and small per-page string tables.
local L={language='zh',chosen=false}
local cache,page=nil,nil
local root=notes.resolve('../ui')
local file='/storage/flash/factory-language.json'
function L.text(zh,en) return L.language=='en' and en or zh end
function L.select(value)
 L.language=value;L.chosen=true;cache=nil;page=nil
 local text=notes.encode({language=value})
 local r=notes.call('/fs','write',{file=file,offset='0',text=text,truncate=true})
 if r.ok then notes.call('/fs','flush',{file=file}) end
 return r
end
function L.restore(done)
 local function read()
  local r=notes.call('/fs','read',{file=file,offset='0',length=256})
  if r.ok and type(r.result.hex)=='string' then local ok,p=pcall(notes.decode,notes.unhex(r.result.hex));if ok and (p.language=='zh' or p.language=='en') then L.language=p.language;L.chosen=true end end
  done()
 end
 local state=notes.call('/dev/flash','read',{})
 if state.ok and state.result.mounted then read();return end
 local r=notes.request('/dev/flash','mount',{},read)
 if not r.ok then read() end
end
function L.values(name,values)
 local result={}
 if L.language=='en' then
  if page~=name then
   page=name;cache={};local r=notes.call('/fs','read',{file=root..'/'..(name=='apps' and 'menu' or name)..'.en.json',offset='0',length=1024})
   if r.ok then local ok,strings=pcall(notes.decode,notes.unhex(r.result.hex));if ok then cache=strings end end
  end
  for key,value in pairs(cache) do result[key]=value end
 end
 for key,value in pairs(values) do result[key]=value end
 return result
end
return L
