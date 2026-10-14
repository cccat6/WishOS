#include "FileService.h"
#include <cassert>
#include <iostream>
using namespace notesos;
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
 std::cout<<"Shared file API preserves binary text, validation-before-effects, leases, exclusive create, resize, flush, rename and 64-bit EOF offsets\n";
}
