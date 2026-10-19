#include "Core.h"
#include <thread>
#include <vector>
#include <atomic>
#include <cassert>
#include <iostream>
static constexpr int pdPASS=1;static bool rejectTask=false;static std::vector<std::thread>threads;
static int xTaskCreate(void(*run)(void*),const char*,unsigned,void*context,unsigned,void*){if(rejectTask)return 0;threads.emplace_back([=]{run(context);});return pdPASS;}
static void vTaskDelete(void*){}
int flashFormatStatus=0;
#include "drivers/esp32-services/FlashVolumeService.h"
using namespace notesos;
static void join(){for(auto&t:threads)t.join();threads.clear();}
int main(){Core core;Vfs vfs;fs::LittleFSFS fs;bool mounted=false;LittleFsProvider provider(fs,mounted,"/tmp/wishos-flash-test");vfs.add(provider);FlashVolumeService service(core,vfs,fs,provider,mounted,"/tmp/wishos-flash-test","test");core.add(Device::bind("/dev/flash","read,mount,format,space,cancel","",service,"read"));JsonDocument a,o;
 rejectTask=true;assert(service.invoke("mount",JsonObjectConst(),o.to<JsonObject>()));assert(!mounted&&!provider.busyPath("/")&&core.resources().promised()==0);rejectTask=false;
 std::atomic<bool>hold{true};fs.hold=&hold;assert(!core.request("/dev/flash","mount",JsonObjectConst(),o.to<JsonObject>()));uint32_t completion=o["completion_id"];assert(service.active());
 for(unsigned i=0;i<100;++i){assert(!service.invoke("read",JsonObjectConst(),o.to<JsonObject>()));assert(o["mounted"]==false);core.clock(i);service.poll();}
 hold=false;join();service.poll();assert(mounted&&!provider.busyPath("/"));assert(!core.completions().read(completion,0,o.to<JsonObject>()));assert(o["ready"]==true&&o["event"]=="operation_completed");core.completions().release(completion,0);assert(core.resources().promised()==0);
 assert(vfs.pin(provider,77));assert(!strcmp(service.invoke("format",JsonObjectConst(),o.to<JsonObject>()),"storage_in_use"));assert(fs.ends==0);
 hold=true;assert(!service.invoke("space",JsonObjectConst(),o.to<JsonObject>()));assert(mounted);FileInfo info;assert(!strcmp(provider.stat("/anything",info),"resource_busy"));hold=false;join();service.poll();vfs.unpin(77);
 flashFormatStatus=-1;assert(!service.invoke("format",JsonObjectConst(),o.to<JsonObject>()));join();service.poll();assert(!mounted&&fs.ends==1);service.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["operation"]["error"]=="format_failed");flashFormatStatus=0;
 assert(!service.invoke("mount",JsonObjectConst(),o.to<JsonObject>()));join();service.poll();assert(mounted);assert(core.resources().promised()==0);
 // The system volume facade consumes only its selector. Native operations
 // keep /dev/flash correlation, system format ownership and cancellation ids.
 struct Selected {FlashVolumeService&native;const char*invoke(const char*m,JsonObjectConst a,JsonObject o){return native.invokeSelected(m,a,o);}void describe(JsonObject o){native.describe(o);}} selected{service};
 assert(core.add(Device::bind("/sys/storage","mount,format,space,cancel","",selected)));
 auto begins=fs.begins,ends=fs.ends;auto sequence=core.sequence();
 for(auto method:{"mount","format","space","cancel"}){a.clear();a["provider"]="elsewhere";assert(!strcmp(core.invoke("/sys/storage",method,a.as<JsonObjectConst>(),o.to<JsonObject>()),"storage_provider_not_found"));}
 a.clear();a["provider"]="flash";a["operation_id"]=99;assert(!strcmp(core.invoke("/sys/storage","mount",a.as<JsonObjectConst>(),o.to<JsonObject>()),"unknown_argument"));
 a.remove("operation_id");auto actor=core.acquireOwner();assert(!strcmp(core.invoke("/sys/storage","format",a.as<JsonObjectConst>(),o.to<JsonObject>(),actor),"system_scope_required"));
 assert(fs.begins==begins&&fs.ends==ends&&core.sequence()==sequence&&!service.active());
 hold=true;assert(!core.request("/sys/storage","space",a.as<JsonObjectConst>(),o.to<JsonObject>(),actor));completion=o["completion_id"];
 auto operation=o["operation_id"].as<uint32_t>();a["operation_id"]=operation+1;assert(!strcmp(core.invoke("/sys/storage","cancel",a.as<JsonObjectConst>(),o.to<JsonObject>(),actor),"operation_not_found"));
 a["operation_id"]=operation;assert(!strcmp(core.invoke("/sys/storage","cancel",a.as<JsonObjectConst>(),o.to<JsonObject>(),core.acquireOwner()),"operation_owned_by_other"));
 assert(!core.invoke("/sys/storage","cancel",a.as<JsonObjectConst>(),o.to<JsonObject>(),actor));service.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["operation"]["operation_id"]==operation&&o["operation"]["cancel_requested"]==true);
 hold=false;join();service.poll();assert(!core.completions().read(completion,actor,o.to<JsonObject>()));assert(o["ready"]==true&&o["source"]=="/dev/flash"&&o["data"]["operation_id"]==operation);assert(!core.completions().release(completion,actor));
 a.clear();a["provider"]="flash";assert(!core.request("/sys/storage","format",a.as<JsonObjectConst>(),o.to<JsonObject>()));completion=o["completion_id"];join();service.poll();assert(fs.ends==ends+1&&mounted);
 assert(!core.completions().read(completion,0,o.to<JsonObject>()));assert(o["event"]=="operation_completed"&&o["source"]=="/dev/flash");assert(!core.completions().release(completion,0));assert(core.resources().promised()==0);
 std::cout<<"Flash async preparation, nonblocking cached reads, leases, failure cleanup and recovery passed\n";
}
