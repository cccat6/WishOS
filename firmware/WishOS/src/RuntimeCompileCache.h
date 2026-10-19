#pragma once
#include "ResourceBudget.h"
#include "Sha256.h"
#include "Vfs.h"
extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
}
namespace notesos {
// Process-local source and compiled prototypes share one bounded LRU. Providers
// remain authoritative: callers resolve and lease a currently available source
// before consulting its nonzero revision. Execution state stays in each VM.
class RuntimeCompileCache {
 enum class Kind {Code,Source};
 struct SourceKey {const FileProvider*provider;uint64_t revision;};
 struct Entry {Entry*next;size_t bytes,payloadBytes;Kind kind;union {char digest[65];SourceKey source;};char data[1];};
 ResourceBudget&resources_;Entry*head_=nullptr;size_t limit_=0,used_=0;unsigned entries_=0;
 uint32_t hits_=0,misses_=0,sourceHits_=0,sourceMisses_=0;ResourceBudget::Region region_=ResourceBudget::Region::Auto;
 void remove(Entry**link){auto e=*link;*link=e->next;used_-=e->bytes;--entries_;resources_.freeAllocation(e);}
 void evict(){auto link=&head_;while((*link)->next)link=&(*link)->next;remove(link);}
 void promote(Entry**link){auto e=*link;*link=e->next;e->next=head_;head_=e;}
 Entry*allocate(Kind kind,const char*path,size_t size){
  size_t pathBytes=strlen(path)+1;
  if(size>limit_||sizeof(Entry)+pathBytes>limit_-size)return nullptr;
  size_t bytes=sizeof(Entry)+pathBytes+size;
  while(head_&&bytes>limit_-used_)evict();
  auto e=static_cast<Entry*>(resources_.allocate(bytes,region_));if(!e)return nullptr;
  e->bytes=bytes;e->payloadBytes=size;e->kind=kind;memcpy(e->data,path,pathBytes);return e;
 }
 void insert(Entry*e){e->next=head_;head_=e;used_+=e->bytes;++entries_;}
 void retain(lua_State*L,const char*path,const char*digest){
  size_t codeBytes=0;
  if(lua_dump(L,[](lua_State*,const void*,size_t n,void*p){auto&total=*static_cast<size_t*>(p);if(n>SIZE_MAX-total)return 1;total+=n;return 0;},&codeBytes,0))return;
  auto e=allocate(Kind::Code,path,codeBytes);if(!e)return;strcpy(e->digest,digest);
  struct Writer {uint8_t*data;size_t size,at=0;}writer{reinterpret_cast<uint8_t*>(e->data+strlen(path)+1),codeBytes};
  int status=lua_dump(L,[](lua_State*,const void*p,size_t n,void*context){auto&w=*static_cast<Writer*>(context);if(n>w.size-w.at)return 1;memcpy(w.data+w.at,p,n);w.at+=n;return 0;},&writer,0);
  if(status||writer.at!=codeBytes){resources_.freeAllocation(e);return;}insert(e);
 }
public:
 explicit RuntimeCompileCache(ResourceBudget&r):resources_(r){}
 ~RuntimeCompileCache(){clear();}
 RuntimeCompileCache(const RuntimeCompileCache&)=delete;
 RuntimeCompileCache&operator=(const RuntimeCompileCache&)=delete;
 void clear(){while(head_)remove(&head_);}
 const char*configure(JsonObjectConst a){
  for(auto f:a)if(strcmp(f.key().c_str(),"compile_cache_bytes")&&strcmp(f.key().c_str(),"compile_cache_memory"))return "unknown_runtime_setting";
  if(a.containsKey("compile_cache_bytes")&&!a["compile_cache_bytes"].is<uint32_t>())return "runtime_cache_type";
  size_t limit=a["compile_cache_bytes"]|uint32_t(limit_);auto region=region_;
  if(limit>resources_.limit())return "runtime_cache_range";
  if(a.containsKey("compile_cache_memory")){
   const char*value=a["compile_cache_memory"]|"";
   if(!strcmp(value,"auto"))region=ResourceBudget::Region::Auto;
   else if(!strcmp(value,"internal"))region=ResourceBudget::Region::Internal;
   else if(!strcmp(value,"external"))region=ResourceBudget::Region::External;
   else return "memory_region";
   if(region==ResourceBudget::Region::External){JsonDocument status;resources_.read(status.to<JsonObject>());if(status.overflowed())return "memory_low";bool present=false;for(auto heap:status["heaps"].as<JsonArrayConst>())if(heap["id"]=="external")present=true;if(!present)return "memory_region_unavailable";}
  }
  if(region!=region_)clear();region_=region;limit_=limit;while(used_>limit_)evict();return nullptr;
 }
 void read(JsonObject out)const{
  out["compile_cache_bytes"]=limit_;out["compile_cache_memory"]=ResourceBudget::name(region_);out["compile_cache_used_bytes"]=used_;out["compile_cache_entries"]=entries_;out["compile_cache_hits"]=hits_;out["compile_cache_misses"]=misses_;out["source_cache_hits"]=sourceHits_;out["source_cache_misses"]=sourceMisses_;
 }
 // Returned source is an owned copy under the existing Reader contract. No VM
 // borrows an LRU entry across a configuration change, eviction or module load.
 bool copySource(const FileProvider&provider,const char*path,uint64_t revision,char*&text,size_t&size){
  if(!limit_)return false;
  invalidateSource(provider,path,revision);
  if(revision)for(auto link=&head_;*link;link=&(*link)->next){auto e=*link;
   if(e->kind!=Kind::Source||e->source.provider!=&provider||strcmp(e->data,path))continue;
   auto copy=static_cast<char*>(malloc(e->payloadBytes));
   if(!copy){remove(link);break;}
   memcpy(copy,e->data+strlen(path)+1,e->payloadBytes);text=copy;size=e->payloadBytes-1;
   promote(link);++sourceHits_;return true;
  }
  ++sourceMisses_;return false;
 }
 void invalidateSource(const FileProvider&provider,const char*path,uint64_t revision){
  for(auto link=&head_;*link;){auto e=*link;
   if(e->kind==Kind::Source&&((e->source.provider==&provider&&(!revision||e->source.revision!=revision))||(e->source.provider!=&provider&&!strcmp(e->data,path))))remove(link);
   else link=&e->next;
  }
 }
 void rememberSource(const FileProvider&provider,const char*path,uint64_t revision,const char*text,size_t size){
  if(!limit_||!revision||size==SIZE_MAX)return;
  auto e=allocate(Kind::Source,path,size+1);if(!e)return;
  e->source={&provider,revision};memcpy(e->data+strlen(path)+1,text,size);e->data[strlen(path)+1+size]=0;insert(e);
 }
 int load(lua_State*L,const char*path,const char*source,size_t size){
  if(!limit_)return luaL_loadbufferx(L,source,size,path,"t");
  Sha256 hash;hash.update(reinterpret_cast<const uint8_t*>(source),size);char digest[65];hash.finish(digest);
  for(auto link=&head_;*link;link=&(*link)->next){auto e=*link;if(e->kind!=Kind::Code||strcmp(e->data,path))continue;
   if(strcmp(e->digest,digest)){remove(link);break;}
   promote(link);++hits_;
   return luaL_loadbufferx(L,e->data+strlen(path)+1,e->payloadBytes,path,"b");
  }
  ++misses_;int status=luaL_loadbufferx(L,source,size,path,"t");if(status==LUA_OK)retain(L,path,digest);return status;
 }
};
}
