// Execute every factory application through the real runtime, UI, files,
// lifecycle and callbacks. Peripheral protocol completion is a fixture; target
// builds and physical tests remain separate.
#include "UiMemory.h"
#include "PackedFont.h"
#include "Core.h"
#include "Policy.h"
#include "PresentationState.h"
#include "DisplayScheduler.h"
#include "display/NativeText.h"
#include "ScriptHost.h"
#include "Applications.h"
#include "FileService.h"
#include "drivers/esp32-services/PosixFileProvider.h"
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cassert>
#include <vector>
static uint64_t nowMs=0;uint32_t millis(){return nowMs;}
static int wallOffsetSeconds=0;
namespace notesos {
namespace boardpack {constexpr unsigned DISPLAY_WIDTH=320,DISPLAY_HEIGHT=240;}
static Core core;static Vfs vfs;static char appScope[48]="system";
static const Setting settings[]={Setting{"/sys/ui","font_file","string","\"/fonts/NotesSansSC-Regular.ttf\"","",0,255}.inSystem(),{"/sys/ui","png_decode_budget_bytes","integer","262144","",1,1048576}};
static Policies policies(settings,2);
static bool resolveFile(const char*in,char*full,const char*&rel,FileProvider*&p){if(!normalizePath("/",in,full,256))return false;p=vfs.resolve(full,rel);return p;}
#include "UiService.h"
static const char*defaultUiDriver(){return "fixture.rgb565";}static const char*validateUiDriver(const char*){return nullptr;}
static unsigned job=0;
static DisplayAdmission queueUi(const char*path,const char*,bool,JsonObject out,const char*,const UiView*view,PresentationState::Stamp stamp){uiLeaseOwner=++job+1000;if(!vfs.lockRead(path,uiLeaseOwner))return DisplayAdmission::rejected("lease");bool ok=prepareUi(path,*view);UiSink sink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};if(ok)ok=renderUiRegion({0,0,319,239},sink);vfs.release(uiLeaseOwner);uiLeaseOwner=0;if(!ok)return DisplayAdmission::rejected(uiError);presentationState.completed(stamp);commitUiGeometry(stamp);out["job_id"]=job;JsonDocument e;e["view_id"]=stamp.view;e["view_revision"]=stamp.revision;e["job_id"]=job;core.emit("/dev/display","completed",e.as<JsonObjectConst>(),view->owner);return {};}
static ScriptHost*host=nullptr;static Applications*apps=nullptr;static FileService*files=nullptr;static MemoryFiles*memoryVolume=nullptr;static bool sdMounted=false,camReady=false,micActive=false;static unsigned camFrame=0,nextCameraFrame=1,micId=0,aiId=0,operations=0;static uint64_t cameraAt=0;static std::vector<uint8_t>cameraBytes(320*240*2,0x44);
static const char*complete(const char*path,const char*method,JsonObject out){JsonDocument e;e["operation_id"]=++operations;e["method"]=method;e["ok"]=true;out["operation_id"]=operations;core.emit(path,"operation_completed",e.as<JsonObjectConst>());return nullptr;}
static const char*camera(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"read")){o["ready"]=camReady;o["operation_pending"]=false;o["frame_id"]=camFrame;if(camFrame){o["bytes"]=cameraBytes.size();o["width"]=320;o["height"]=240;}return nullptr;}if(!strcmp(m,"configure"))return nullptr;if(!strcmp(m,"start")){camReady=true;return complete("/dev/camera",m,o);}if(!strcmp(m,"capture")){camFrame=nextCameraFrame++;cameraAt=nowMs;return complete("/dev/camera",m,o);}if(!strcmp(m,"release")){camFrame=0;return nullptr;}if(!strcmp(m,"read_chunk")){size_t at=std::stoull(a["offset"].as<const char*>()),n=std::min<size_t>(a["length"]|1024u,cameraBytes.size()-at);char bytes[2049];hex(cameraBytes.data()+at,n,bytes);o["hex"]=bytes;sizeText(o,"next_offset",at+n);return nullptr;}return "unsupported_method";}
static void step(unsigned count=1){static uint32_t cursor=0;while(count--){nowMs+=20;core.clock(nowMs);core.tick();core.dispatch(cursor);host->tick(nowMs);apps->poll();files->poll();}}
static JsonDocument state(){JsonDocument s;host->invoke("read",JsonObjectConst(),s.to<JsonObject>());if(s["fault"]==true){serializeJson(s,std::cerr);assert(false);}return s;}
static void launch(const char*id){JsonDocument a,o;a["id"]=id;assert(!apps->invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));step(4);auto s=state();assert(s["loaded"]==true);JsonDocument x;apps->invoke("read",JsonObjectConst(),x.to<JsonObject>());if(x["current"]!=id){serializeJson(x,std::cerr);assert(false);}}
static void touch(unsigned x,unsigned y){JsonDocument e;e["operation_id"]=99;e["count"]=1;e["contacts"][0]["x"]=x;e["contacts"][0]["y"]=y;core.emit("/dev/touch","completed",e.as<JsonObjectConst>());step(2);e["count"]=0;e["contacts"].to<JsonArray>();core.emit("/dev/touch","completed",e.as<JsonObjectConst>());step(2);state();}
}
int main(){using namespace notesos;char temp[]="/tmp/wishos-suite-XXXXXX";assert(mkdtemp(temp));std::filesystem::copy("hardware/agent-os/runtime/lckfb-suite/assets",temp,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);bool ready=true;PosixFileProvider factory("factory","/factory","host",false,ready,temp,"","host",1),fontProvider("fonts","/fonts","host",false,ready,"hardware/agent-os/runtime/fonts","","host",1),system("system","/system","host",false,ready,"firmware/WishOS","","host",1);fontProvider.writable=system.writable=false;MemoryFiles ram("ram","/storage/ram","ram"),card("user","/storage/user","memory");card.budget(1048576);vfs.add(factory);vfs.add(fontProvider);vfs.add(system);vfs.add(ram);vfs.add(card);FileService fileApi(vfs,&core);files=&fileApi;memoryVolume=&ram;ScriptHost vm(core,vfs);host=&vm;Applications manager(core,vfs,vm,"launcher","voice");apps=&manager;
 JsonDocument defaults;auto list=defaults.to<JsonArray>();for(auto id:{"launcher","voice","device","camera","files","settings"}){auto a=list.add<JsonObject>();a["id"]=id;a["title"]=id;a["manifest"]="/factory/apps/"+std::string(id)+".json";}std::string encoded;serializeJson(defaults,encoded);assert(manager.defaults(encoded.c_str()));
 core.add(Device::bind("/sys/apps",Applications::Methods,"",manager,"read,list"));core.add(Device::bind("/fs",FileService::Methods,"",fileApi,"read,stat,list"));core.add({"/sys/ui","read,open,update,hit_test","",uiHandler,uiAsyncDescription,"read,hit_test"});core.add({"/sys/timers","write,remove","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return core.timer(a,o);core.removeTimer(a["id"]|0u);return nullptr;}});
 core.add({"/sys/config","read,write","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"read")){policies.read("system",o);return nullptr;}return policies.apply(a,o);},nullptr,"read"});core.add({"/sys/storage","configure","",[](const char*,JsonObjectConst a,JsonObject)->const char*{return memoryVolume->budget(a["quota_bytes"]|0u)?nullptr:"memory_volume_in_use";}});
 core.add({"/dev/wifi","read,configure","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["connected"]=true;o["ssid"]="Home";o["ip"]="192.168.0.2";o["mac"]="02:00:00:00:00:01";return nullptr;},nullptr,"read"});core.add({"/services/link","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["connected"]=true;return nullptr;},nullptr,"read"});core.add({"/services/time","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{int seconds=12*3600+int(nowMs/1000)+wallOffsetSeconds;char text[40];snprintf(text,sizeof text,"2026-10-16T%02d:%02d:%02d+0800",seconds/3600,(seconds/60)%60,seconds%60);o["wall_clock_valid"]=true;o["local_time"]=text;return nullptr;},nullptr,"read"});
 core.add({"/dev/touch","measure","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["operation_id"]=++operations;JsonDocument e;e["count"]=0;e["contacts"].to<JsonArray>();core.emit("/dev/touch","completed",e.as<JsonObjectConst>());return nullptr;}});
 core.add({"/dev/sd","read,mount","",[](const char*m,JsonObjectConst,JsonObject o)->const char*{if(!strcmp(m,"mount")){sdMounted=true;return complete("/dev/sd",m,o);}o["mounted"]=sdMounted;o["operation_pending"]=false;return nullptr;},[](JsonObject o){asyncContract(o,"mount","/dev/sd",{"operation_completed","operation_failed"})["operation_id"]="operation_id";},"read"});
 core.add({"/dev/imu","configure,measure","",[](const char*m,JsonObjectConst,JsonObject o)->const char*{if(!strcmp(m,"configure"))return nullptr;JsonDocument e;for(auto key:{"accel_x_g","accel_y_g","gyro_x_dps","gyro_y_dps","gyro_z_dps"})e[key]=0.;e["accel_z_g"]=1.;e["die_temperature_c"]=25.;e["operation_id"]=++operations;o["operation_id"]=operations;core.emit("/dev/imu","completed",e.as<JsonObjectConst>());return nullptr;}});
 core.add({"/dev/camera","read,configure,start,capture,read_chunk,release","",camera,[](JsonObject o){for(auto m:{"start","capture"})asyncContract(o,m,"/dev/camera",{"operation_completed","operation_failed"})["operation_id"]="operation_id";},"read,read_chunk"});
 core.add({"/net/http-server","route","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;}});core.add({"/services/nfs","start","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;}});
 core.add({"/dev/buttons","read","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;},nullptr,"read"});core.add({"/services/ai","read,submit,read_action,acknowledge,release","",[](const char*m,JsonObjectConst,JsonObject o)->const char*{if(!strcmp(m,"submit"))o["id"]=++aiId;else if(!strcmp(m,"read"))o["tasks"].to<JsonArray>();return nullptr;},[](JsonObject o){asyncContract(o,"submit","/services/ai")["id"]="id";},"read,read_action"});
 core.add({"/dev/mic","read,start,stop","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;}});
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return core.describe(a["path"]|"",o);},nullptr,"describe"});core.add({"/sys/operations","request,read,release","",[](const char*m,JsonObjectConst a,JsonObject o){return core.operation(m,a,o);}});
 core.add({"/sys/clock","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["monotonic_ms"]=nowMs;o["unix_s"]=1792160000;return nullptr;},nullptr,"read"});
 core.setEventObserver([](const Event&e){return host->event(e);});
 launch("launcher");assert(strstr(uiCurrent,"language.json"));touch(70,190);assert(strstr(uiCurrent,"welcome.json"));touch(130,218);assert(strstr(uiCurrent,"home.json")&&sdMounted);touch(210,112);assert(strstr(uiCurrent,"apps.json"));launch("launcher");assert(strstr(uiCurrent,"home.json"));
 // The desktop follows wall time, not a fixed snapshot or elapsed timer count.
 // Same-minute polls do not submit frames; a forward tick and backward NTP
 // correction update once, without navigating away from another launcher page.
 {unsigned before=job;step(50);assert(job==before);wallOffsetSeconds+=60;step(50);assert(job==before+1);assert(strstr(uiDocument(presentationState.active())->view.values,"12:01"));
  wallOffsetSeconds-=60;JsonDocument sync;core.emit("/services/time","synchronized",sync.as<JsonObjectConst>());step();assert(job==before+2&&strstr(uiDocument(presentationState.active())->view.values,"12:00"));
  touch(210,112);assert(strstr(uiCurrent,"apps.json"));before=job;wallOffsetSeconds+=60;step(50);assert(job==before&&strstr(uiCurrent,"apps.json"));launch("launcher");assert(strstr(uiDocument(presentationState.active())->view.values,"12:01"));}
 launch("settings");touch(60,193);launch("device");step(30);assert(strstr(uiDocument(presentationState.active())->view.values,"1.00"));launch("files");state();launch("voice");assert(strstr(uiCurrent,"voice.json"));
 for(size_t i=0;i<cameraBytes.size();++i)cameraBytes[i]=(i&1)?0x1f:0xf8;
 launch("camera");step(200);auto r=state();std::cerr<<"camera VM peak="<<r["memory_peak"].as<unsigned>()<<"\n";assert(!camFrame);assert(strstr(uiDocument(presentationState.active())->view.values,"wishos-camera"));assert(r["fault"]==false);touch(240,215);step(180);assert(strstr(uiDocument(presentationState.active())->view.values,"wishos-camera"));JsonDocument listed;assert(!card.list("/WishOS/photos",0,8,listed.to<JsonObject>())&&listed["entries"].size()==1);std::string saved="/WishOS/photos/"+listed["entries"][0]["name"].as<std::string>();uint8_t pixel[14];size_t got=0;assert(!card.read(saved.c_str(),0,pixel,sizeof pixel,got)&&got==14&&pixel[12]==0x1f&&pixel[13]==0xf8);touch(65,215);step(180);assert(strstr(uiDocument(presentationState.active())->view.values,"camera-2.bin"));launch("launcher");step(5);FileInfo info;assert(!strcmp(ram.stat("/wishos-camera-1.bin",info),"not_found"));
 JsonDocument a,o;host->invoke("stop",a.as<JsonObjectConst>(),o.to<JsonObject>());fileApi.poll();resetUiRenderer();core.setEventObserver(nullptr);std::filesystem::remove_all(temp);std::cout<<"Factory launcher language/onboarding, touch navigation, saved settings, app entry/exit, IMU updates and bounded camera copying passed\n";
}
