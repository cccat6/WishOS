#pragma once
#include "RuntimeContract.h"
#include "Vfs.h"
extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
}
namespace notesos {
// The same text compiler as the runtime, with no libraries, callbacks or execution.
struct LuaSyntax {
 static constexpr size_t SourceLimit=LuaSourceLimit;
 struct Memory {ResourceBudget&resources;size_t limit,used=0;
  static void*resize(void*p,void*old,size_t,size_t n){auto&m=*static_cast<Memory*>(p);size_t previous=m.resources.allocationSize(old);if(n>previous&&n-previous>m.limit-m.used)return nullptr;auto next=m.resources.reallocate(old,n);if(next||!n)m.used=m.used-previous+n;return next;}
 };
 static const char*compile(Core&core,const char*source,size_t size,JsonObject out,size_t limit=0){
  if(!size||size>SourceLimit||memchr(source,0,size))return "script_size";
  Memory memory{core.resources(),limit?limit:core.resources().defaultVmLimit()};auto state=lua_newstate(Memory::resize,&memory);if(!state)return "syntax_memory_budget";
  int status=luaL_loadbufferx(state,source,size,"application","t");if(status!=LUA_OK)out["detail"]=lua_tostring(state,-1);lua_close(state);
  if(status)return status==LUA_ERRMEM?"syntax_memory_budget":"script_syntax";out["parser"]=LUA_RELEASE;out["compiled"]=true;out["executed"]=false;return nullptr;
 }
 static const char*file(Core&core,Vfs&vfs,const char*path,JsonObject out,size_t limit=0){
  const char*relative;auto provider=vfs.resolve(path,relative);if(!provider||!provider->available())return "application_storage_unavailable";Vfs::Access lease(vfs,false);if(auto e=lease.add(path,provider->caseInsensitive()))return e;
  FileInfo info;if(auto e=provider->stat(relative,info))return e;if(info.directory||!info.size||info.size>SourceLimit)return "script_size";
  Memory memory{core.resources(),limit?limit:core.resources().defaultVmLimit()};auto state=lua_newstate(Memory::resize,&memory);if(!state)return "syntax_memory_budget";
  struct Reader {FileProvider*provider;const char*path;uint64_t size,at=0;const char*error=nullptr;uint8_t bytes[512];}reader{provider,relative,info.size};
  int status=lua_load(state,[](lua_State*,void*p,size_t*n)->const char*{auto&r=*static_cast<Reader*>(p);*n=0;if(r.error||r.at==r.size)return nullptr;size_t got=0;r.error=r.provider->read(r.path,r.at,r.bytes,std::min<uint64_t>(sizeof r.bytes,r.size-r.at),got);if(!r.error&&!got)r.error="script_short_read";if(!r.error&&memchr(r.bytes,0,got))r.error="script_size";if(r.error)return nullptr;r.at+=got;*n=got;return reinterpret_cast<const char*>(r.bytes);},&reader,"application","t");
  if(status&&!reader.error)out["detail"]=lua_tostring(state,-1);lua_close(state);if(reader.error)return reader.error;if(status)return status==LUA_ERRMEM?"syntax_memory_budget":"script_syntax";out["parser"]=LUA_RELEASE;out["compiled"]=true;out["executed"]=false;return nullptr;
 }
};
}
