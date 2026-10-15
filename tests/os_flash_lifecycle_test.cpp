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
 std::cout<<"Flash async preparation, nonblocking cached reads, leases, failure cleanup and recovery passed\n";
}
