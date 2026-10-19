#include "ScriptHost.h"
#include "drivers/esp32-services/PosixFileProvider.h"
#include <fstream>
#include <filesystem>
#include <cassert>
#include <iostream>
using namespace notesos;
static unsigned writes=0;static int value=0;
static const char*sink(const char*,JsonObjectConst a,JsonObject){++writes;value=a["value"]|0;return nullptr;}
static JsonDocument parse(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static void put(MemoryFiles&f,const char*path,const char*text){size_t n;assert(!f.write(path,0,reinterpret_cast<const uint8_t*>(text),strlen(text),true,n));}
struct SourceFiles:MemoryFiles {
 unsigned reads=0,stats=0;bool present=true,revisionless=false,changeOnRead=false;
 SourceFiles(const char*id="source",const char*mount="/source"):MemoryFiles(id,mount,"source"){}
 bool available()const override{return present;}
 uint64_t changeRevision()const override{return revisionless?0:MemoryFiles::changeRevision();}
 const char*stat(const char*p,FileInfo&out)override{++stats;return MemoryFiles::stat(p,out);}
 const char*read(const char*p,uint64_t at,uint8_t*out,size_t n,size_t&got)override{++reads;auto error=MemoryFiles::read(p,at,out,n,got);if(changeOnRead){size_t wrote;assert(!MemoryFiles::write("/revision-marker",0,nullptr,0,true,wrote));}return error;}
};
static JsonDocument state(ScriptHost&h){JsonDocument d;assert(!h.invoke("read",JsonObjectConst(),d.to<JsonObject>()));return d;}
static size_t externalUsed=0;
static void*externalResize(void*p,size_t n){auto old=p?static_cast<size_t*>(p)-1:nullptr;size_t previous=old?*old:0;if(!n){externalUsed-=previous;free(old);return nullptr;}auto next=static_cast<size_t*>(realloc(old,n+sizeof(size_t)));if(!next)return nullptr;*next=n;externalUsed=externalUsed-previous+n;return next+1;}
static void load(ScriptHost&h,const char*path){JsonDocument a,out;a["file"]=path;a["memory_bytes"]=65536;auto e=h.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>());if(e){std::cerr<<e<<" ";serializeJson(out,std::cerr);}assert(!e);}
int main(){
 Core core;core.add({"/sink","write","",sink});SourceFiles files;Vfs vfs;vfs.add(files);JsonDocument out;
 assert(!files.mkdir("/one"));assert(!files.mkdir("/two"));
 const char*main=R"(local m=notes.require('module.lua'); counter=(counter or 0)+1
 function on_start() assert(counter==1 and m.count==1);m.count=m.count+1;notes.call('/sink','write',{value=m.value}) end
 function on_prepare() assert(counter==1 and m.count==1) end
 function on_event() notes.call('/sink','write',{value=88});return true end
 function on_stop() end)";
 put(files,"/one/main.lua",main);put(files,"/one/module.lua","return {count=1,value=7}");
 put(files,"/two/main.lua",main);put(files,"/two/module.lua","return {count=1,value=11}");
 {
  ScriptHost host(core,vfs);auto initial=state(host);assert(initial["compile_cache_bytes"]==0);
  load(host,"/source/one/main.lua");assert(value==7&&files.reads==2);assert(state(host)["compile_cache_entries"]==0);
  auto config=parse(R"({"compile_cache_bytes":16000,"compile_cache_memory":"internal"})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));
  load(host,"/source/one/main.lua");auto first=state(host);assert(first["compile_cache_entries"]==4&&first["compile_cache_misses"]==2&&first["compile_cache_hits"]==0);
  auto resident=first["compile_cache_used_bytes"].as<unsigned>();assert(resident>0);
  load(host,"/source/one/main.lua");auto warm=state(host);assert(files.reads==4&&files.stats==4&&warm["compile_cache_hits"]==2&&warm["compile_cache_misses"]==2&&value==7&&warm["source_cache_hits"]==2);
  {Vfs::Access writer(vfs,true);assert(!writer.add("/source/one/main.lua"));auto a=parse(R"({"file":"/source/one/main.lua"})");assert(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["compile_cache_hits"]==2);}
  // Both VMs executed their own main/module code and constructed fresh state.
  put(files,"/one/module.lua","return {count=1,value=9}");load(host,"/source/one/main.lua");assert(value==9);auto changed=state(host);assert(changed["compile_cache_hits"]==3&&changed["compile_cache_misses"]==3&&changed["compile_cache_entries"]==4);
  load(host,"/source/two/main.lua");auto distinct=state(host);assert(value==11&&distinct["compile_cache_misses"]==5&&distinct["compile_cache_entries"]==8);
  // Configuration rejection keeps both the previous policy and live cache.
  config=parse(R"({"compile_cache_bytes":0,"compile_cache_memory":"invalid"})");assert(host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["compile_cache_entries"]==8&&state(host)["compile_cache_bytes"]==16000);
  config=parse(R"({"compile_cache_bytes":0,"compile_cache_memory":"external"})");assert(host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["compile_cache_bytes"]==16000);
  config=parse(R"({"compile_cache_bytes":-1})");assert(host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));
  // Capacity reduction evicts least-recently-used chunks immediately.
  resident=distinct["compile_cache_used_bytes"].as<unsigned>()-changed["compile_cache_used_bytes"].as<unsigned>();config.clear();config["compile_cache_bytes"]=resident;assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));auto reduced=state(host);assert(reduced["compile_cache_used_bytes"].as<unsigned>()<=resident&&reduced["compile_cache_entries"].as<unsigned>()<8);
  auto misses=reduced["compile_cache_misses"].as<unsigned>();load(host,"/source/two/main.lua");assert(state(host)["compile_cache_misses"]==misses); // latest pair retained
  // Changed provider revision invalidates source facts before any cache hit.
  assert(!files.remove("/two/module.lua"));auto a=parse(R"({"file":"/source/two/main.lua","memory_bytes":65536})");auto owner=state(host)["owner"].as<unsigned>();assert(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["owner"]==owner&&state(host)["fault"]==false);
  put(files,"/two/module.lua","return {count=1,value=11}");
  // Failed preparation preserves the live foreground, including on a cache hit.
  put(files,"/bad.lua","function on_prepare() error('candidate rejected') end");a["file"]="/source/bad.lua";
  assert(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["owner"]==owner&&state(host)["fault"]==false);
  size_t binaryWritten;assert(!files.write("/binary.lua",0,CALLBACK_BYTECODE,sizeof CALLBACK_BYTECODE,true,binaryWritten));a["file"]="/source/binary.lua";assert(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["owner"]==owner);
  // Disabling frees every cached block while leaving the current VM usable.
  config=parse(R"({"compile_cache_bytes":0})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["compile_cache_entries"]==0&&state(host)["compile_cache_used_bytes"]==0);
  assert(!host.invoke("stop",JsonObjectConst(),out.to<JsonObject>()));assert(core.resources().promised()==0);
  // A small cap declines storage rather than declining a valid application.
  config["compile_cache_bytes"]=1;assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));load(host,"/source/one/main.lua");assert(value==9&&state(host)["compile_cache_entries"]==0);
 }
 assert(core.resources().promised()==0);
 core.resources().heap(ResourceBudget::Region::External,{[](){return size_t(100000);},[](){return size_t(100000)-externalUsed;},[](){return size_t(100000)-externalUsed;},externalResize});
 {ScriptHost host(core,vfs);auto config=parse(R"({"compile_cache_bytes":16000,"compile_cache_memory":"external"})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));auto a=parse(R"({"source":"function on_start() end"})");assert(!host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(externalUsed>0&&state(host)["compile_cache_entries"]==1);}
 assert(externalUsed==0&&core.resources().promised()==0);
 // Cache heap failure remains optional; the VM can run in its own memory pool.
 core.resources().heap(ResourceBudget::Region::External,{[](){return size_t(100000);},[](){return size_t(100000);},[](){return size_t(100000);},[](void*,size_t)->void*{return nullptr;}});
 auto policy=parse(R"({"application_memory":"internal"})");assert(!core.resources().configure(policy.as<JsonObjectConst>()));
 {
  ScriptHost host(core,vfs);auto config=parse(R"({"compile_cache_bytes":16000,"compile_cache_memory":"external"})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));auto a=parse(R"({"source":"function on_start() notes.call('/sink','write',{value=19}) end","memory_bytes":65536})");assert(!host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(value==19&&state(host)["compile_cache_entries"]==0);
 }
 assert(core.resources().promised()==0);
 // Failed source caching uses the normal read/compile path and leaves no entry.
 put(files,"/simple.lua","function on_start() notes.call('/sink','write',{value=29}) end");
 {ScriptHost host(core,vfs);auto config=parse(R"({"compile_cache_bytes":16000,"compile_cache_memory":"external"})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));auto before=files.reads;load(host,"/source/simple.lua");load(host,"/source/simple.lua");assert(files.reads==before+2&&value==29&&state(host)["compile_cache_entries"]==0);}
 // Every reuse checks provider availability and a fresh lease. A returned source
 // is owned independently of cache eviction, reconfiguration and VM lifetime.
 {
  Core c;c.add({"/sink","write","",sink});SourceFiles original,other("other","/other");Vfs fs;assert(fs.add(original)&&fs.add(other));
  put(original,"/main.lua","function on_start() notes.call('/sink','write',{value=31}) end");
  put(other,"/main.lua","function on_start() notes.call('/sink','write',{value=41}) end");
  ScriptHost host(c,fs);auto config=parse(R"({"compile_cache_bytes":16000})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));
  load(host,"/source/main.lua");load(host,"/source/main.lua");assert(original.reads==1&&original.stats==1&&value==31);
  auto owner=state(host)["owner"].as<unsigned>();original.present=false;auto a=parse(R"({"file":"/source/main.lua","memory_bytes":65536})");assert(!strcmp(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()),"provider_unavailable"));assert(state(host)["owner"]==owner);
  original.present=true;load(host,"/source/main.lua");assert(original.reads==2&&value==31);
  // Same pathname on replacement provider must read its own source even when
  // both provider generations happen to have the same numeric revision.
  original.mount="/old";other.mount="/source";load(host,"/source/main.lua");assert(other.reads==1&&value==41);
  original.mount="/source";other.mount="/other";load(host,"/source/main.lua");assert(original.reads==3&&value==31);
  // Separate mounts of identical relative names retain distinct identities.
  load(host,"/other/main.lua");assert(value==41&&other.reads==2);load(host,"/source/main.lua");assert(value==31&&original.reads==3);
  // A zero revision explicitly disables source reuse; whole-source SHA still
  // allows compiled reuse, while a text edit invalidates its prototype.
  original.revisionless=true;auto reads=original.reads;load(host,"/source/main.lua");load(host,"/source/main.lua");assert(original.reads==reads+2);
  put(original,"/main.lua","function on_start() notes.call('/sink','write',{value=51}) end");load(host,"/source/main.lua");assert(value==51&&original.reads==reads+3);
  // Do not retain a source read across a provider generation change.
  original.revisionless=false;original.changeOnRead=true;load(host,"/source/main.lua");reads=original.reads;original.changeOnRead=false;load(host,"/source/main.lua");assert(original.reads==reads+1&&value==51);
  // Writer admission also applies to warm modules, preserving the old VM.
  put(original,"/module.lua","return 61");put(original,"/main.lua","local m=notes.require('module.lua');function on_start() notes.call('/sink','write',{value=m}) end");load(host,"/source/main.lua");load(host,"/source/main.lua");owner=state(host)["owner"].as<unsigned>();
  {Vfs::Access writer(fs,true);assert(!writer.add("/source/module.lua"));assert(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(state(host)["owner"]==owner);}
  load(host,"/source/main.lua");assert(value==61);
 }
 // Removable media can change outside the provider's write generation. A new
 // execution reads actual media, while code reuse still verifies the full text.
 {
  char directory[]="/tmp/wishos-source-cache-media-XXXXXX";assert(mkdtemp(directory));
  Core c;c.add({"/sink","write","",sink});bool mounted=true;PosixFileProvider media("sd","/sd","sd",true,mounted,directory,"","fat",512);Vfs fs;assert(fs.add(media));
  {ScriptHost host(c,fs);auto config=parse(R"({"compile_cache_bytes":16000})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));
   auto path=std::string(directory)+"/main.lua";auto write=[&](int value){std::ofstream stream(path);stream<<"function on_start() notes.call('/sink','write',{value="<<value<<"}) end";};
   write(71);load(host,"/sd/main.lua");load(host,"/sd/main.lua");assert(value==71);auto revision=media.changeRevision();auto status=state(host);assert(status["source_cache_hits"]==0&&status["compile_cache_hits"]==1);
   write(81);assert(revision==media.changeRevision()&&media.available());load(host,"/sd/main.lua");assert(value==81);auto owner=state(host)["owner"].as<unsigned>();
   assert(std::filesystem::remove(path));auto a=parse(R"({"file":"/sd/main.lua","memory_bytes":65536})");assert(!strcmp(host.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()),"not_found"));assert(state(host)["owner"]==owner);
  }
  assert(c.resources().promised()==0);std::filesystem::remove_all(directory);
 }
 // A removable adapter that supplies a reliable cross-operation content epoch
 // can opt into reuse, independently of its local write generation value.
 {
  struct ReliableMedia:SourceFiles {
   ReliableMedia(){removable=true;}
   uint64_t contentRevision()const override{return MemoryFiles::changeRevision()+100;}
  }media;
  Core c;c.add({"/sink","write","",sink});Vfs fs;assert(fs.add(media));
  put(media,"/main.lua","function on_start() notes.call('/sink','write',{value=87}) end");
  {ScriptHost host(c,fs);auto config=parse(R"({"compile_cache_bytes":16000})");assert(!host.invoke("configure",config.as<JsonObjectConst>(),out.to<JsonObject>()));
   load(host,"/source/main.lua");load(host,"/source/main.lua");assert(value==87&&media.stats==1&&media.reads==1&&state(host)["source_cache_hits"]==1);
   put(media,"/main.lua","function on_start() notes.call('/sink','write',{value=88}) end");load(host,"/source/main.lua");assert(value==88&&media.stats==2&&media.reads==2);
   load(host,"/source/main.lua");assert(value==88&&media.reads==2&&state(host)["source_cache_hits"]==2);
  }
  assert(c.resources().promised()==0);
 }
 // Source bytes and native prototypes occupy the same bounded LRU. A copied
 // Reader result survives both immediate eviction and full cache destruction.
 {
  Core c;SourceFiles provider;RuntimeCompileCache cache(c.resources());auto config=parse(R"({"compile_cache_bytes":800})");assert(!cache.configure(config.as<JsonObjectConst>()));const char text[]="return 123";
  cache.rememberSource(provider,"/source/a.lua",provider.changeRevision(),text,strlen(text));char*owned=nullptr;size_t bytes=0;assert(cache.copySource(provider,"/source/a.lua",provider.changeRevision(),owned,bytes));assert(bytes==strlen(text)&&!strcmp(owned,text));
  lua_State*L=luaL_newstate();assert(L);assert(cache.load(L,"/source/b.lua",text,strlen(text))==LUA_OK);lua_pop(L,1);JsonDocument status;cache.read(status.to<JsonObject>());assert(status["compile_cache_entries"]==2);
  // Shrinking keeps the most recent compiled entry and evicts older source.
  config["compile_cache_bytes"]=status["compile_cache_used_bytes"].as<unsigned>()-1;assert(!cache.configure(config.as<JsonObjectConst>()));cache.read(status.to<JsonObject>());assert(status["compile_cache_entries"]==1);
  char*miss=nullptr;size_t n=0;assert(!cache.copySource(provider,"/source/a.lua",provider.changeRevision(),miss,n));assert(!strcmp(owned,text));
  config["compile_cache_bytes"]=0;assert(!cache.configure(config.as<JsonObjectConst>()));assert(!strcmp(owned,text));free(owned);lua_close(L);
 }
 // A fresh system host has no cache or execution state from the prior process.
 {ScriptHost host(core,vfs);assert(state(host)["compile_cache_entries"]==0&&state(host)["compile_cache_bytes"]==0);}
 std::cout<<"Runtime source/compiled-block cache: source freshness, path identity, fresh VMs, LRU, atomic preparation, rejection and allocation failure passed\n";
}
