#include "FileService.h"
#include "FileReadCache.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static unsigned allocations=0;static size_t live=0;static bool deny=false;static unsigned denyFrom=0;
static void*allocate(size_t bytes){++allocations;if(deny||(denyFrom&&allocations>=denyFrom))return nullptr;auto p=(size_t*)malloc(bytes+sizeof(size_t));if(!p)return nullptr;*p=bytes;live+=bytes;return p+1;}
static void release(void*ptr){if(ptr){auto p=(size_t*)ptr-1;live-=*p;free(p);}}
int main(){
 Vfs vfs;MemoryFiles memory("ram","/storage/ram","ram");vfs.add(memory);FileService service(vfs);
 JsonDocument args,result;auto call=[&](const char*method,const char*json){args.clear();assert(!deserializeJson(args,json));return service.invoke(method,args.as<JsonObjectConst>(),result.to<JsonObject>());};
 assert(!call("mounts","{}"));assert(result["providers"][0]["operations"].is<const char*>());
 assert(!call("write",R"({"file":"/storage/ram/资料","text":"a\u0000b","truncate":true})"));assert(result["written"]==3);
 assert(!call("read",R"({"file":"/storage/ram/资料","length":3})"));assert(result["hex"]=="610062");
 assert(call("write",R"({"file":"/storage/ram/资料","text":"new","hex":"00","truncate":true})"));
 assert(call("write",R"({"file":"/storage/ram/资料","text":"new","truncate":"true"})"));
 assert(call("resize",R"({"file":"/storage/ram/资料","size":"-1"})"));
 assert(!call("read",R"({"file":"/storage/ram/资料"})"));assert(result["hex"]=="610062");
 assert(vfs.lockRead("/storage/ram/资料",17));assert(!strcmp(call("resize",R"({"file":"/storage/ram/资料","size":1})"),"file_in_use"));
 vfs.release(17);assert(!call("resize",R"({"file":"/storage/ram/资料","size":5})"));
 assert(!call("read",R"({"file":"/storage/ram/资料","offset":"2","length":3})"));assert(result["hex"]=="620000");
 assert(!call("flush",R"({"file":"/storage/ram/资料"})"));
 assert(!call("create",R"({"file":"/storage/ram/new"})"));assert(call("create",R"({"file":"/storage/ram/new"})"));
 assert(!call("rename",R"({"file":"/storage/ram/资料","to":"/storage/ram/renamed"})"));assert(call("stat",R"({"file":"/storage/ram/资料"})"));
 assert(!call("read",R"({"file":"/storage/ram/renamed","offset":"4294967300"})"));assert(result["hex"]=="");
 assert(call("read",R"({"file":"/storage/ram/renamed","path":"/storage/ram/other"})"));
 assert(!call("remove",R"({"file":"/storage/ram/renamed"})"));
 // A device may mount its default provider at root; nested mounts still win.
 MemoryFiles root("root","/","ram");assert(vfs.add(root));const char*relative=nullptr;
 assert(vfs.resolve("/",relative)==&root&&!strcmp(relative,"/"));
 assert(vfs.resolve("/book",relative)==&root&&!strcmp(relative,"/book"));
 assert(vfs.resolve("/storage/ram/new",relative)==&memory&&!strcmp(relative,"/new"));
 assert(vfs.resolve("/storage/rambling",relative)==&root&&!strcmp(relative,"/storage/rambling"));
 char absolute[256];assert(root.absolutePath("/book",absolute,sizeof absolute)&&!strcmp(absolute,"/book"));
 assert(memory.absolutePath("/book",absolute,sizeof absolute)&&!strcmp(absolute,"/storage/ram/book"));
 assert(!memory.absolutePath("/../outside",absolute,sizeof absolute));
 assert(!call("write",R"({"file":"/book","text":"root data"})"));
 assert(vfs.lockRead("/book",19));assert(!strcmp(call("remove",R"({"file":"/book"})"),"file_in_use"));
 vfs.release(19);assert(!call("read",R"({"file":"/book"})"));assert(result["hex"]=="726f6f742064617461");
 // Empty adapters do not preallocate file indexes. Metadata grows only for
 // actual files and is reclaimed, including a rejected data allocation.
 { MemoryFiles dynamic("dynamic","/dynamic","ram",true,allocate,release);JsonDocument state;dynamic.describe(state.to<JsonObject>());assert(allocations==0&&live==0);
   for(unsigned i=0;i<40;++i){char name[32];snprintf(name,sizeof name,"/file%u",i);assert(!dynamic.createFile(name));}assert(live>0);for(unsigned i=0;i<40;++i){char name[32];snprintf(name,sizeof name,"/file%u",i);assert(!dynamic.remove(name));}assert(live==0);
   deny=true;size_t wrote=0;assert(dynamic.write("/denied",0,(const uint8_t*)"data",4,true,wrote));deny=false;FileInfo info;assert(dynamic.stat("/denied",info));assert(live==0);denyFrom=allocations+2;assert(dynamic.write("/data-denied",0,(const uint8_t*)"data",4,true,wrote));denyFrom=0;assert(dynamic.stat("/data-denied",info));assert(live==0);
 }
 // All eight providers remain reachable through the bounded control envelope.
 MemoryFiles extra[]={{"p1","/p1","ram"},{"p2","/p2","ram"},{"p3","/p3","ram"},{"p4","/p4","ram"},{"p5","/p5","ram"},{"p6","/p6","ram"}};for(auto&p:extra)assert(vfs.add(p));
 unsigned cursor=0,seen=0;for(;;){args.clear();args["cursor"]=cursor;args["limit"]=1;assert(!service.invoke("mounts",args.as<JsonObjectConst>(),result.to<JsonObject>()));assert(result["providers"].size()==1);++seen;cursor=result["next_cursor"];if(!result["more"])break;}assert(seen==8&&cursor==8);assert(call("mounts",R"({"limit":0})"));assert(call("mounts",R"({"cursor":"0"})"));
 {struct Source:MemoryFiles{unsigned reads=0;bool online=true,fail=false;Source():MemoryFiles("cache","/cache","ram"){}bool available()const override{return online;}const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{++reads;if(fail){got=0;return "read_failed";}return MemoryFiles::read(p,at,b,n,got);}} source;
   uint8_t original[1000];for(unsigned i=0;i<sizeof original;++i)original[i]=uint8_t(i);size_t wrote,got;assert(!source.write("/file",0,original,sizeof original,true,wrote));FileReadCache<> cache(source,"/file");uint8_t bytes[20];for(auto offset:{0,300,600,900})assert(!cache.read(offset,bytes,20,got)&&got==20&&bytes[0]==uint8_t(offset));auto reads=source.reads;assert(!cache.read(1,bytes,20,got)&&source.reads==reads&&bytes[0]==1);assert(!cache.read(1000,bytes,20,got)&&got==0);
   const uint8_t update=99;assert(!source.write("/file",0,&update,1,false,wrote));assert(!cache.read(0,bytes,1,got)&&got==1&&bytes[0]==99);source.online=false;assert(!strcmp(cache.read(0,bytes,1,got),"provider_unavailable"));source.online=true;source.fail=true;assert(!strcmp(cache.read(4096,bytes,1,got),"read_failed")&&got==0);source.fail=false;assert(!cache.read(4096,bytes,1,got)&&got==0);
 }
 // Temporary files retain entry identity across rename and defer deletion
 // until a renderer/native reader releases its lease. A reused name is safe.
 {Core core;FileService owned(vfs,&core);auto actor=core.acquireOwner();core.add(Device::bind("/owned-fs",FileService::Methods,"",owned));JsonDocument a,o;a["file"]="/storage/ram/temp";a["temporary"]=true;assert(!core.invoke("/owned-fs","create",a.as<JsonObjectConst>(),o.to<JsonObject>(),actor));
  assert(!memory.rename("/temp","/renamed-temp"));assert(!memory.createFile("/temp"));assert(vfs.lockRead("/storage/ram/renamed-temp",900));core.releaseOwner(actor);owned.poll();FileInfo i;assert(!memory.stat("/renamed-temp",i));vfs.release(900);owned.poll();assert(!strcmp(memory.stat("/renamed-temp",i),"not_found"));assert(!memory.stat("/temp",i));assert(!memory.remove("/temp"));
  a["file"]="/storage/ram/no-owner";assert(!strcmp(core.invoke("/owned-fs","create",a.as<JsonObjectConst>(),o.to<JsonObject>()),"temporary_file_owner_required"));assert(!strcmp(memory.stat("/no-owner",i),"not_found"));
 }
 std::cout<<"Shared file API preserves binary text, validation-before-effects, leases, exclusive create, resize, flush, rename and 64-bit EOF offsets\n";
}
