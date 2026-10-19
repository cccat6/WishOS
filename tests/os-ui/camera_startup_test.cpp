#include <atomic>
#include <thread>
#include <vector>
#include <mutex>
#include <chrono>
#include <cassert>
#include <iostream>
#include "drivers/esp-camera/EspCamera.h"
std::atomic<uint64_t>cameraClock{0};std::vector<std::thread>cameraTasks;
static std::mutex busMutex;static std::atomic<bool>busHeld{false},initialized{false},powered{false},leased{false},psramMode{false},blockFrame{false},enteredFrame{false};
static std::atomic<unsigned>frameGets{0},returns{0};static uint64_t frameAdvanceMs=30;static bool failExposure=false;static int failGet=0,failDeinit=0;static bool exposure=false,white=false,gain=false;static uint8_t pixels[]={0xf8,0,0x07,0xe0,0,0x1f,0xff,0xff};static camera_fb_t frame{pixels,8,2,2,PIXFORMAT_RGB565,{0,0}};
static int simple(sensor_t*,int){assert(busHeld);return 0;}static int ae(sensor_t*,int n){assert(busHeld);if(failExposure)return -1;exposure=n;return 0;}static int awb(sensor_t*,int n){assert(busHeld);white=n;return 0;}static int agc(sensor_t*,int n){assert(busHeld);gain=n;return 0;}static sensor_t sensor{{0x9b},simple,simple,simple,ae,awb,agc};
int esp_camera_set_psram_mode(bool on){psramMode=on;return initialized?ESP_OK:ESP_ERR_INVALID_STATE;}bool esp_camera_get_psram_mode(){return psramMode;}int esp_camera_init(const camera_config_t*){assert(powered&&!initialized);initialized=true;return ESP_OK;}int esp_camera_deinit(){assert(!leased);if(failDeinit){--failDeinit;return ESP_FAIL;}initialized=false;return ESP_OK;}sensor_t*esp_camera_sensor_get(){return initialized?&sensor:nullptr;}
camera_fb_t*esp_camera_fb_get(){assert(initialized&&!leased&&!busHeld);enteredFrame=true;while(blockFrame)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(failGet&&++frameGets==unsigned(failGet))return nullptr;if(!failGet)++frameGets;leased=true;cameraClock+=frameAdvanceMs;frame.timestamp.tv_sec=cameraClock/1000;frame.timestamp.tv_usec=(cameraClock%1000)*1000;return &frame;}
void esp_camera_fb_return(camera_fb_t*p){assert(p==&frame&&leased);leased=false;++returns;}
static const char*power(bool on){powered=on;return nullptr;}
using namespace notesos;
struct PersistentFiles:MemoryFiles{PersistentFiles():MemoryFiles("flash","/flash","flash"){persistent=true;}};
int main(){
 {drivers::EspCameraClock clock;JsonDocument out;
 assert(!clock.apply(20000000));clock.read(out.to<JsonObject>());assert(out["applied_hz"]==20000000&&out["source_hz"]==160000000&&out["integer_divider"]==8);clock.release();assert(cameraClockLeases==0);
 assert(!clock.apply(24000000));clock.read(out.to<JsonObject>());assert(out["applied_hz"]==24000000&&out["source_hz"]==240000000&&out["integer_divider"]==10);assert(LCD_CAM.cam_ctrl.cam_update==1);clock.release();
 assert(!clock.apply(25000000));clock.read(out.to<JsonObject>());assert(out["source_hz"]==160000000&&out["integer_divider"]==6&&out["fraction_denominator"]==5&&out["fraction_numerator"]==2);clock.release();
 assert(!clock.apply(23000000));clock.read(out.to<JsonObject>());assert(out["integer_divider"]==6&&out["fraction_denominator"]==23&&out["fraction_numerator"]==22);clock.release();
 auto source=cameraClockSource;auto divider=LCD_CAM.cam_ctrl.cam_clkm_div_num;assert(clock.apply(23000001)&&clock.apply(0));assert(cameraClockSource==source&&LCD_CAM.cam_ctrl.cam_clkm_div_num==divider&&!cameraClockLeases);
 cameraClockEnableFailure=true;assert(!strcmp(clock.apply(24000000),"camera_clock_enable_failed")&&!cameraClockLeases);cameraClockEnableFailure=false;
 cameraClockReadbackFailure=true;assert(!strcmp(clock.apply(24000000),"camera_clock_not_applied")&&cameraClockLeases==1);clock.release();cameraClockReadbackFailure=false;assert(!cameraClockLeases);}
 Core core;Vfs vfs;MemoryFiles ram("ram","/ram","ram");PersistentFiles flash;vfs.add(ram);vfs.add(flash);size_t written;assert(!ram.write("/view",0,nullptr,0,true,written));assert(!flash.write("/view",0,nullptr,0,true,written));core.resources().heap(ResourceBudget::Region::External,{[]()->size_t{return 8*1024*1024;},[]()->size_t{return 8*1024*1024;},nullptr,nullptr});
 drivers::I2cBus bus{nullptr,[](void*){if(!busMutex.try_lock())return false;busHeld=true;return true;},[](void*){busHeld=false;busMutex.unlock();},nullptr};
 {drivers::EspCamera camera(core,vfs,bus,camera_config_t{},power,"GC0308",{true,true,true},{3,150});core.add(Device::bind("/dev/camera","read,describe,configure,start,capture,release,cancel,stop,read_chunk,copy_frame","",camera));
 auto call=[&](const char*m,JsonObjectConst a,JsonObject o,uint32_t owner=0){return core.invoke("/dev/camera",m,a,o,owner);};
 auto settle=[&](){for(unsigned n=0;n<2000;++n){camera.poll();JsonDocument status;camera.invoke("read",JsonObjectConst(),status.to<JsonObject>());if(!status["operation_pending"].as<bool>())return status;std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(false);return JsonDocument();};
 JsonDocument a,o;a["warmup_frames"]=6;a["auto_exposure"]=false;a["frame_size"]="bad";assert(call("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));a.clear();assert(!call("read",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(o["configuration"]["warmup_frames"]==3&&o["configuration"]["auto_exposure"]==true);
 uint32_t owner=core.acquireOwner();assert(!call("start",JsonObjectConst(),o.to<JsonObject>(),owner));auto ready=settle();assert(ready["ready"]==true&&ready["startup"]["discarded_frames"].as<unsigned>()>=3&&ready["startup"]["elapsed_ms"].as<unsigned>()>=150);assert(exposure&&white&&gain&&!leased&&frameGets==returns&&!busHeld);
 assert(!call("capture",JsonObjectConst(),o.to<JsonObject>(),owner));auto captured=settle();auto id=captured["frame_id"].as<uint32_t>();assert(id&&captured["captured_at_us"].is<const char*>());a.clear();a["frame_id"]=id;a["file"]="/flash/view";assert(!strcmp(call("copy_frame",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner),"volatile_destination_required"));a["file"]="/ram/view";a["pixel_format"]="rgb565le";assert(!call("copy_frame",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));uint8_t copied[8];size_t count;assert(!ram.read("/view",0,copied,sizeof copied,count)&&count==8&&copied[0]==0&&copied[1]==0xf8);a.clear();a["frame_id"]=id;uint32_t other=core.acquireOwner();assert(!strcmp(call("read_chunk",a.as<JsonObjectConst>(),o.to<JsonObject>(),other),"camera_frame_owned_by_other"));assert(!strcmp(call("stop",JsonObjectConst(),o.to<JsonObject>(),other),"camera_owned_by_other"));core.releaseOwner(other);failDeinit=1;core.releaseOwner(owner);auto closed=settle();assert(closed["initialized"]==true&&closed["ready"]==false&&closed["lifecycle_owner"]==0);other=core.acquireOwner();assert(!call("stop",JsonObjectConst(),o.to<JsonObject>(),other));settle();core.releaseOwner(other);for(unsigned i=0;i<20&&camera.active();++i){camera.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(!camera.active()&&!initialized&&!leased&&!powered&&frameGets==returns);
 // Cancellation during startup returns the SDK-owned buffer and completes all
 // native cleanup before budget release; no outer bus lock spans frame waits.
 enteredFrame=false;blockFrame=true;owner=core.acquireOwner();a.clear();a["warmup_frames"]=100;assert(!call("configure",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));assert(!call("start",JsonObjectConst(),o.to<JsonObject>(),owner));auto op=o["operation_id"].as<uint32_t>();for(unsigned n=0;n<1000&&!enteredFrame;++n)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(enteredFrame);assert(bus.acquire(bus.context));bus.release(bus.context);JsonDocument live;assert(!call("read",JsonObjectConst(),live.to<JsonObject>()));assert(live["operation_pending"]==true&&live["clock"].isNull()&&live["configuration"]["requested_xclk_hz"]==24000000);a.clear();a["operation_id"]=op;assert(!call("cancel",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));blockFrame=false;auto cancelled=settle();assert(cancelled["ready"]==false&&!initialized&&!leased&&!powered);core.releaseOwner(owner);
 // Failed clock verification cleans up the SDK and its clock reference.
 cameraClockReadbackFailure=true;assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));auto clockFailed=settle();assert(clockFailed["operation"]["error"]=="camera_clock_not_applied"&&!initialized&&!powered&&!cameraClockLeases);cameraClockReadbackFailure=false;
 // A failed clock readback plus failed SDK teardown retains the source lease
 // until a later successful native stop; it cannot release clock still in use.
 cameraClockReadbackFailure=true;failDeinit=1;assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));clockFailed=settle();assert(!clockFailed["ready"].as<bool>()&&clockFailed["initialized"]==true&&clockFailed["clock"]["source_lease"]==true&&cameraClockLeases==1);assert(!call("stop",JsonObjectConst(),o.to<JsonObject>()));settle();assert(!initialized&&!powered&&!cameraClockLeases);cameraClockReadbackFailure=false;
 // Failed setters and missing startup frames do not leave a ready camera.
 failExposure=true;assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));auto failed=settle();assert(failed["ready"]==false&&!initialized&&!powered);failExposure=false;failGet=int(frameGets)+1;assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));failed=settle();assert(failed["operation"]["error"]=="camera_warmup_timeout"&&!initialized&&!powered);failGet=0;
 frame.format=PIXFORMAT_JPEG;assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));failed=settle();assert(failed["operation"]["error"]=="camera_frame_format"&&!initialized&&!powered&&!leased);frame.format=PIXFORMAT_RGB565;
 a.clear();a["warmup_frames"]=0;a["warmup_ms"]=0;a["auto_exposure"]=false;a["auto_white_balance"]=false;a["auto_gain"]=false;assert(!call("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));ready=settle();assert(ready["startup"]["discarded_frames"]==0&&!exposure&&!white&&!gain);
 // A configured uint32 duration may cross the 32-bit millis wrap in one
 // frame. The SDK monotonic clock must complete it without exact-tick matching.
 assert(!call("stop",JsonObjectConst(),o.to<JsonObject>()));settle();
 a.clear();a["warmup_ms"]=UINT32_MAX-5u;assert(!call("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));
 auto before=frameGets.load();frameAdvanceMs=uint64_t(UINT32_MAX)+100u;
 assert(!call("start",JsonObjectConst(),o.to<JsonObject>()));ready=settle();
 assert(ready["ready"]==true&&frameGets==before+1&&ready["startup"]["discarded_frames"]==1);
 assert(strtoull(ready["startup"]["elapsed_ms"]|"0",nullptr,10)>UINT32_MAX);frameAdvanceMs=30;
 failDeinit=1;assert(!call("stop",JsonObjectConst(),o.to<JsonObject>()));failed=settle();assert(failed["ready"]==false&&failed["initialized"]==true&&failed["clock"]["source_lease"]==true&&cameraClockLeases==1&&core.resources().promised()>0);assert(!strcmp(call("start",JsonObjectConst(),o.to<JsonObject>()),"camera_cleanup_required"));assert(core.resources().promised()>0);assert(!call("stop",JsonObjectConst(),o.to<JsonObject>()));settle();assert(!initialized&&!powered&&!cameraClockLeases);
 }for(auto&t:cameraTasks)t.join();cameraTasks.clear();assert(core.resources().promised()==0);
 {drivers::EspCamera basic(core,vfs,bus,camera_config_t{},power);JsonDocument d,a,o;basic.describe(d.to<JsonObject>());assert(!d["settings"].containsKey("auto_exposure"));a["auto_exposure"]=true;assert(!strcmp(basic.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()),"camera_control_unavailable"));assert(!powered);}
 // The same adapter uses SDK-declared DRAM without any external heap.
 auto drain=[](drivers::EspCamera&camera){for(unsigned i=0;i<2000;++i){camera.poll();JsonDocument out;camera.invoke("read",JsonObjectConst(),out.to<JsonObject>());if(!out["operation_pending"].as<bool>())return out;std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(false);return JsonDocument();};
 {Core local;local.resources().heap(ResourceBudget::Region::Internal,{[]()->size_t{return 180000;},[]()->size_t{return 180000;},nullptr,nullptr});camera_config_t cfg{};cfg.frame_size=FRAMESIZE_QQVGA;cfg.fb_location=CAMERA_FB_IN_DRAM;
 drivers::EspCamera camera(local,vfs,bus,cfg,power);JsonDocument d,a,o;camera.describe(d.to<JsonObject>());assert(d["framebuffer_region"]=="internal"&&d["framebuffer_count"]==1);
 a["psram_dma"]=true;a["frame_size"]="vga";assert(!strcmp(camera.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()),"camera_psram_dma_requires_psram_frames"));camera.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["configuration"]["frame_size"]=="qqvga"&&o["configuration"]["psram_dma"]==false);
 assert(!camera.invoke("start",JsonObjectConst(),o.to<JsonObject>()));assert(drain(camera)["ready"]==true&&!psramMode);assert(local.resources().promised()>100000);
 assert(!camera.invoke("stop",JsonObjectConst(),o.to<JsonObject>()));drain(camera);assert(local.resources().promised()==0);}
 for(auto&t:cameraTasks)t.join();cameraTasks.clear();
 {Core local;local.resources().heap(ResourceBudget::Region::External,{[]()->size_t{return 150000;},[]()->size_t{return 150000;},nullptr,nullptr});camera_config_t cfg{};cfg.frame_size=FRAMESIZE_QQVGA;cfg.fb_count=2;
 drivers::EspCamera camera(local,vfs,bus,cfg,power);JsonDocument o,d;camera.describe(d.to<JsonObject>());assert(d["framebuffer_region"]=="external"&&d["framebuffer_count"]==2);assert(!strcmp(camera.invoke("start",JsonObjectConst(),o.to<JsonObject>()),"camera_memory_budget"));assert(!powered&&local.resources().promised()==0);
 local.resources().heap(ResourceBudget::Region::External,{[]()->size_t{return 300000;},[]()->size_t{return 300000;},nullptr,nullptr});assert(!camera.invoke("start",JsonObjectConst(),o.to<JsonObject>()));assert(drain(camera)["ready"]==true);assert(local.resources().promised()>2*38400);assert(!camera.invoke("stop",JsonObjectConst(),o.to<JsonObject>()));drain(camera);assert(local.resources().promised()==0);}
 for(auto&t:cameraTasks)t.join();cameraTasks.clear();
 // The non-direct SDK path reserves its actual configured DMA-buffer bound,
 // plus the pre-existing control/task workspace. Its framebuffer stays external.
 {Core local;local.resources().heap(ResourceBudget::Region::Internal,{[]()->size_t{return 60000;},[]()->size_t{return 60000;},nullptr,nullptr});local.resources().heap(ResourceBudget::Region::External,{[]()->size_t{return 300000;},[]()->size_t{return 300000;},nullptr,nullptr});camera_config_t cfg{};
 drivers::EspCamera camera(local,vfs,bus,cfg,power);JsonDocument a,o;a["psram_dma"]=false;assert(!camera.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!camera.invoke("start",JsonObjectConst(),o.to<JsonObject>()));assert(drain(camera)["ready"]==true&&!psramMode);assert(!camera.invoke("stop",JsonObjectConst(),o.to<JsonObject>()));drain(camera);assert(local.resources().promised()==0);}
 for(auto&t:cameraTasks)t.join();cameraTasks.clear();
 // Source declarations with no buffers or unsupported pixel formats fail
 // before power, task creation, or resource retention.
 for(unsigned n=0;n<3;++n){Core local;camera_config_t cfg{};if(n==0)cfg.fb_count=0;if(n==1)cfg.pixel_format=PIXFORMAT_JPEG;if(n==2)cfg.fb_count=SIZE_MAX;drivers::EspCamera camera(local,vfs,bus,cfg,power);JsonDocument o;assert(camera.invoke("start",JsonObjectConst(),o.to<JsonObject>()));assert(!powered&&local.resources().promised()==0&&cameraTasks.empty());}
 // Exercise the real native conversion across every 1 KiB copy boundary,
 // including the offset where the observed physical frame changed byte phase.
 {Core local;local.resources().heap(ResourceBudget::Region::External,{[]()->size_t{return 300000;},[]()->size_t{return 300000;},nullptr,nullptr});drivers::EspCamera camera(local,vfs,bus,camera_config_t{},power);JsonDocument a,o;
 std::vector<uint8_t>source(320*240*2);for(size_t i=0;i<source.size();++i)source[i]=uint8_t((i*37+i/11)&255);auto original=source;frame.buf=source.data();frame.len=source.size();frame.width=320;frame.height=240;assert(ram.budget(524288));assert(!ram.createFile("/streamcopy"));
 assert(!camera.invoke("start",JsonObjectConst(),o.to<JsonObject>()));drain(camera);assert(!camera.invoke("capture",JsonObjectConst(),o.to<JsonObject>()));auto captured=drain(camera);a["frame_id"]=captured["frame_id"];a["file"]="/ram/streamcopy";a["pixel_format"]="rgb565le";assert(!camera.invoke("copy_frame",a.as<JsonObjectConst>(),o.to<JsonObject>()));std::vector<uint8_t>out(source.size());size_t got;assert(!ram.read("/streamcopy",0,out.data(),out.size(),got)&&got==out.size());for(size_t i=0;i<out.size();++i)assert(out[i]==source[i^1u]);assert(source==original);
 a.remove("file");a.remove("pixel_format");assert(!camera.invoke("release",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!camera.invoke("stop",JsonObjectConst(),o.to<JsonObject>()));drain(camera);assert(!ram.remove("/streamcopy"));frame.buf=pixels;frame.len=8;frame.width=2;frame.height=2;assert(local.resources().promised()==0);}
 for(auto&t:cameraTasks)t.join();cameraTasks.clear();
 assert(cameraClockLeases==0);std::cout<<"Camera exact clocks, startup warmup/frame recycling, sensor controls, cancellation, frame ownership/timestamp and failed cleanup recovery passed\n";
}
