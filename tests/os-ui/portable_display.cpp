// Same runtime/UI code, different geometry and completion models. This fixture
// intentionally has no E6 includes, assets, scene codec or device defaults.
#include "UiMemory.h"
#include "PackedFont.h"
#include "Core.h"
#include "Policy.h"
#include "PresentationState.h"
#include "DisplayScheduler.h"
#include "ScriptHost.h"
#include "display/NativeText.h"
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
uint32_t millis(){static uint32_t t=0;return ++t;}
namespace notesos {
namespace boardpack {constexpr unsigned DISPLAY_WIDTH=TEST_UI_WIDTH,DISPLAY_HEIGHT=TEST_UI_HEIGHT;}
static Core core;static ScriptHost*runtime=nullptr;static Vfs vfs;static char appScope[48]="system";
static const Setting settings[]={{"/sys/ui","png_decode_budget_bytes","integer","16384","",1,1048576}};
static Policies policies(settings,1);
static bool resolveFile(const char*in,char*full,const char*&rel,FileProvider*&p){if(!normalizePath("/",in,full,256))return false;p=vfs.resolve(full,rel);return p;}
#include "UiService.h"
struct Request:DisplayRequest {char path[256]{};UiView view;};
struct Adapter {
 enum Mode{Immediate,Dma,Epd,Scanout}mode=Immediate;
 bool finish=false,fault=false,rendered=false;unsigned starts=0,releases=0;
 std::vector<uint16_t>pixels;unsigned width=0,height=0,rowsPerTurn=0,rasterRow=0;
 DisplayBegin readiness(){return fault?DisplayBegin::failed("device_disconnected"):DisplayBegin{};}
 DisplayBegin begin(Request&r,uint64_t){
  ++starts;uiLeaseOwner=r.id;if(!vfs.lockRead(r.path,r.id))return DisplayBegin::failed("asset_lease_capacity");
  if(!prepareUi(r.path,r.view))return DisplayBegin::failed(uiError);
  width=lv_display_get_horizontal_resolution(uiDisplay);height=lv_display_get_vertical_resolution(uiDisplay);
  pixels.assign(width*height,0);rendered=false;rasterRow=0;return {};
 }
 DisplayOutcome poll(Request&,uint64_t){
  if(!rendered){UiSink sink{this,[](void*context,const lv_area_t&a,const uint16_t*source){
   auto&self=*static_cast<Adapter*>(context);unsigned row=a.x2-a.x1+1;
   if(self.rowsPerTurn)assert(a.y1>=int(self.rasterRow)&&a.y2<int(std::min(self.height,self.rasterRow+self.rowsPerTurn)));
   for(int y=a.y1;y<=a.y2;++y)for(int x=a.x1;x<=a.x2;++x){assert(x>=0&&y>=0&&unsigned(x)<self.width&&unsigned(y)<self.height);self.pixels[y*self.width+x]=source[(y-a.y1)*row+x-a.x1];}
  }};unsigned end=rowsPerTurn?std::min(height,rasterRow+rowsPerTurn):height;lv_area_t area{0,int32_t(rasterRow),int32_t(width)-1,int32_t(end)-1};if(!renderUiRegion(area,sink,rowsPerTurn!=0))return DisplayOutcome::failed(uiError);rasterRow=end;rendered=end==height;if(!rendered)return {};}
  if(mode!=Immediate&&!finish)return {};
  if(fault)return DisplayOutcome::failed("device_disconnected");
  return DisplayOutcome::success(mode==Epd?DisplayEvidence::DeviceReady:mode==Scanout?DisplayEvidence::FrameLatched:DisplayEvidence::TransportComplete,"fixture_native_completion");
 }
 void suspend(Request&r){vfs.release(r.id);uiLeaseOwner=0;}
 void completed(Request&r,const DisplayOutcome&result,uint64_t elapsed){JsonDocument out;result.describe(r,out.to<JsonObject>(),elapsed);core.emit("/dev/display",result.ok()?"completed":"failed",out.as<JsonObjectConst>(),r.owner);}
 void dispose(Request&r){vfs.release(r.id);if(uiLeaseOwner==r.id)uiLeaseOwner=0;++releases;if(!presentationState.active()&&!uiLeaseOwner)resetUiRenderer();}
};
static Adapter adapter;static DisplayScheduler<Request,Adapter>scheduler(presentationState,adapter);
static const char*rejectAdmission=nullptr;static uint32_t nextJob=1000;
static const char*defaultUiDriver(){return "fixture.rgb565";}
static const char*validateUiDriver(const char*s){return strcmp(s,"fixture.rgb565")?"unknown_profile":nullptr;}
static DisplayAdmission queueUi(const char*path,const char*,bool,JsonObject out,const char*,const UiView*view,PresentationState::Stamp stamp){
 if(rejectAdmission)return DisplayAdmission::rejected(rejectAdmission);
 Request r;r.id=nextJob++;r.stamp=stamp;r.owner=presentationState.get(stamp.view)->owner;r.view=*view;copy(r.path,sizeof r.path,path);
 auto result=scheduler.enqueue(r,decltype(scheduler)::Policy::Fifo,2);if(!result.error)out["job_id"]=r.id;return result;
}
static std::vector<JsonDocument>effects;
static const char*effect(const char*,JsonObjectConst a,JsonObject){effects.emplace_back();effects.back().set(a);return nullptr;}
}
int main(){using namespace notesos;MemoryFiles files("memory","/","ram");assert(vfs.add(files));
 const char*page=R"({"version":"0.1.1","assets":[{"type":"viewScreen","style":{"bgColor":"#123456","padding":0},"children":[{"type":"label","placement":{"x":4,"y":4},"labelProps":{"text":"WishOS"},"style":{"fontSize":16,"textColor":"#ffffff"}}]}]})";
 size_t wrote=0;assert(!files.write("/page.json",0,(const uint8_t*)page,strlen(page),true,wrote));
 core.add({"/sys/ui","font_check,read,open,update,refresh,close","",uiHandler,uiServiceDescription,"font_check,read"});
 core.add({"/effect","write","",effect});
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return core.describe(a["path"]|"",o);},nullptr,"describe"});
 core.add({"/sys/timers","write","",[](const char*,JsonObjectConst a,JsonObject o){return core.timer(a,o);}});
 ScriptHost app(core,vfs);runtime=&app;core.setEventObserver([](const Event&e){return runtime->event(e);});JsonDocument load,out,empty;
 load["memory_bytes"]=98304;
 load["source"]=R"lua(
local view
local function done(e) assert(notes.call('/effect','write',{kind='display',event=e.event,error=e.data.error,revision=e.data.view_revision}).ok) end
function on_start()
 notes.on('/input/key','press',function() notes.call('/effect','write',{kind='key'}) end)
 notes.on('/input/change','press',function() assert(notes.request('/sys/ui','update',{view_id=view},done).ok) end)
 notes.on('/sys/timers','fired',function() notes.call('/effect','write',{kind='timer'}) end)
 assert(notes.call('/sys/timers','write',{delay_ms=1}).ok)
 local r=notes.request('/sys/ui','open',{path='/page.json'},done);assert(r.ok,r.error);view=r.result.view_id
end
)lua";
 uint64_t now=0;uint32_t cursor=core.sequence();
 auto pump=[&](){core.clock(++now);core.tick();pollUiPresentation();scheduler.tick(now);core.dispatch(cursor);};
 auto emit=[&](const char*source){JsonDocument data;assert(core.emit(source,"press",data.to<JsonObject>()));core.dispatch(cursor);};
 auto count=[&](const char*kind){unsigned n=0;for(auto&x:effects)n+=x["kind"]==kind;return n;};
 for(auto mode:{Adapter::Immediate,Adapter::Dma,Adapter::Epd,Adapter::Scanout}){
  effects.clear();adapter.mode=mode;adapter.finish=false;adapter.fault=false;
  if(auto error=app.invoke("load",load.as<JsonObjectConst>(),out.to<JsonObject>())){std::cerr<<error<<" ";serializeJson(out,std::cerr);std::cerr<<"\n";abort();}pump();
  emit("/input/key");assert(count("key")==1&&count("timer")==1);
  if(mode!=Adapter::Immediate){assert(scheduler.active());assert(!vfs.canMutate("/page.json"));assert(count("display")==0);}
  // A permanent admission error returns the caller's failure callback once;
  // retrying is an application decision. Existing hardware work keeps running.
  rejectAdmission="profile_resources_unavailable";emit("/input/change");pump();assert(count("display")==unsigned(mode==Adapter::Immediate?2:1));
  unsigned rejected=count("display");for(unsigned i=0;i<3;++i)pump();assert(count("display")==rejected);
  rejectAdmission=nullptr;emit("/input/change");adapter.finish=true;for(unsigned i=0;i<4;++i)pump();
  assert(!scheduler.active()&&!scheduler.pending()&&count("display")==3);
  assert(adapter.width==TEST_UI_WIDTH&&adapter.height==TEST_UI_HEIGHT);
  assert(adapter.pixels.back()==0x11aa); // A true RGB565 color, not six-color quantization.
  assert(!app.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(!presentationState.active());assert(vfs.canMutate("/page.json"));assert(core.resources().promised()==0);
 }
 // The same raster is produced in bounded regions, leaving real input/timer
 // dispatch opportunities between them. No panel completion is emitted early.
 {auto full=adapter.pixels;effects.clear();adapter.mode=Adapter::Immediate;adapter.rowsPerTurn=8;
  assert(!app.invoke("load",load.as<JsonObjectConst>(),out.to<JsonObject>()));pump();unsigned turns=0;
  while(scheduler.active()){assert(!count("display"));emit("/input/key");pump();assert(++turns<=TEST_UI_HEIGHT/8+2);}
  assert(turns>1&&count("key")==turns&&count("display")==1&&adapter.pixels==full);
  assert(!app.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(core.resources().promised()==0);adapter.rowsPerTurn=0;
 }
 // Replace the application while a native refresh owns its source. The old
 // callback disappears, the physical operation still finishes, then the new view
 // receives only its own completion. No product-specific page logic is involved.
 effects.clear();adapter.mode=Adapter::Dma;adapter.finish=false;
 assert(!app.invoke("load",load.as<JsonObjectConst>(),out.to<JsonObject>()));pump();assert(scheduler.active());
 unsigned starts=adapter.starts;assert(!app.invoke("load",load.as<JsonObjectConst>(),out.to<JsonObject>()));
 pump();emit("/input/key");assert(adapter.starts==starts&&scheduler.active()&&count("key")==1&&count("display")==0);
 adapter.finish=true;for(unsigned i=0;i<4;++i)pump();assert(count("display")==1&&!scheduler.pending()&&!scheduler.active());
 assert(!app.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(core.resources().promised()==0);
 // Rotation affects logical layout; native geometry comes from this device,
 // including landscape and odd-sized surfaces, not the E6 panel.
 for(unsigned rotation:{0u,90u,180u,270u}){UiView view;view.rotation=rotation;assert(prepareUi("/page.json",view));assert(lv_display_get_horizontal_resolution(uiDisplay)==int(rotation%180?TEST_UI_HEIGHT:TEST_UI_WIDTH));assert(lv_display_get_vertical_resolution(uiDisplay)==int(rotation%180?TEST_UI_WIDTH:TEST_UI_HEIGHT));}resetUiRenderer();
 // Native failure settles queued requests too; no caller waits forever for a
 // device requiring explicit recovery. Application replacement reclaims callbacks.
 effects.clear();adapter.mode=Adapter::Dma;adapter.finish=false;if(auto error=app.invoke("load",load.as<JsonObjectConst>(),out.to<JsonObject>())){std::cerr<<error<<" ";serializeJson(out,std::cerr);std::cerr<<"\n";abort();}pump();emit("/input/change");adapter.fault=true;adapter.finish=true;pump();pump();assert(count("display")==2&&!scheduler.pending()&&!scheduler.active());
 assert(!app.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(core.resources().promised()==0);
 std::cout<<"Portable UI "<<TEST_UI_WIDTH<<"x"<<TEST_UI_HEIGHT<<": Lua -> render -> immediate/DMA/BUSY/latch -> callbacks, input/timer concurrency and failure recovery passed\n";
}
