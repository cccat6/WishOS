#include "ButtonGestures.h"
#include "drivers/sc7a20/MotionInput.h"
#include "TimeSync.h"
#include "PresentationState.h"
#include "RpcEnvelope.h"
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
using namespace notesos;
static JsonDocument parse(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
struct Captured{std::string name;unsigned count,gesture;bool longPress;unsigned mask,edge,held;};
static std::vector<Captured> events;
static void receive(void*,const char*n,JsonObjectConst d){events.push_back({n,d["count"]|0u,d["gesture_id"]|0u,d["long"]|false,d["pressed_mask"]|0u,d["edge_at_ms"]|0u,d["held_ms"]|0u});}
static unsigned count(const char*n){unsigned out=0;for(auto&e:events)out+=e.name==n;return out;}
struct TimeBackend:TimeSyncBackend{unsigned starts=0,stops=0;int64_t seconds=0;const char*start(const TimeSyncConfig&)override{++starts;return nullptr;}void stop()override{++stops;}int64_t completion()override{auto t=seconds;seconds=0;return t;}};
int main(){
 ButtonGestures b({"left","middle","right"},nullptr,receive);JsonDocument out;
 auto c=parse(R"({"defaults":{"debounce_ms":10,"long_press_ms":600,"click_window_ms":200,"max_clicks":3},"buttons":{"right":{"long_press_ms":1200}}})");
 assert(!b.configure(c.as<JsonObjectConst>(),out.to<JsonObject>()));
 // Bounce does not become a press. Three debounced clicks resolve only once.
 b.sample(1,true,0);b.sample(1,false,3);b.sample(1,true,5);b.sample(1,true,15);assert(count("press")==1);
 b.sample(1,false,30);b.sample(1,false,40);assert(!count("click"));
 b.sample(1,true,90);b.sample(1,true,100);b.sample(1,false,140);b.sample(1,false,150);
 b.sample(1,true,200);b.sample(1,true,210);b.sample(1,false,260);b.sample(1,false,270);
 assert(count("click")==1&&events.back().count==3);assert(count("long_press")==0);
 events.clear();b.sample(1,true,1000);b.sample(1,true,1010);b.sample(1,true,1610);b.sample(1,false,1700);b.sample(1,false,1710);b.sample(1,false,2000);
 assert(count("long_press")==1&&count("release")==1&&!count("click")&&events.back().longPress);
 // Mid-gesture config is deferred. Bad restore/persist preserves the old plan.
 events.clear();b.sample(2,true,3000);b.sample(2,true,3010);
 c=parse(R"({"buttons":{"right":{"long_press_ms":100}}})");assert(!b.configure(c.as<JsonObjectConst>(),out.to<JsonObject>()));assert(out["buttons"]["right"]["pending"]==true);
 b.sample(2,true,3600);assert(!count("long_press"));b.sample(2,true,4210);assert(count("long_press")==1);
 b.sample(2,false,4250);b.sample(2,false,4260);b.sample(2,true,4400);b.sample(2,true,4410);b.sample(2,true,4510);assert(count("long_press")==2);
 assert(!b.restore(R"({"buttons":{"right":{"long_press_ms":0}}})"));b.read(out.to<JsonObject>());assert(out["buttons"]["right"]["configured"]["long_press_ms"]==100);
 c=parse(R"({"defaults":{"max_clicks":1},"persist":true})");assert(b.configure(c.as<JsonObjectConst>(),out.to<JsonObject>(),[](const char*,size_t){return false;}));
 b.read(out.to<JsonObject>());assert(out["buttons"]["middle"]["configured"]["max_clicks"]==3);
 // A single and double resolve at the window, and uint32 clock wrap is safe.
 ButtonGestures wrap({"one"},nullptr,receive);events.clear();wrap.sample(0,true,0xfffffff0);wrap.sample(0,true,20);wrap.sample(0,false,60);wrap.sample(0,false,90);wrap.sample(0,false,391);assert(count("click")==1&&events.back().count==1);
 events.clear();wrap.sample(0,true,500);wrap.sample(0,true,530);wrap.sample(0,false,560);wrap.sample(0,false,590);wrap.sample(0,true,650);wrap.sample(0,true,680);wrap.sample(0,false,710);wrap.sample(0,false,740);wrap.sample(0,false,1041);assert(count("click")==1&&events.back().count==2);
 // Coherent bank snapshots contain both accepted keys, independent of loop
 // iteration order. Edge timestamps retain observed debounce/polling latency.
 ButtonGestures bank({"A","B"},nullptr,receive);events.clear();c=parse(R"({"defaults":{"debounce_ms":10}})");assert(!bank.configure(c.as<JsonObjectConst>(),out.to<JsonObject>()));
 bool down[]={true,true};bank.sampleAll(down,2,1000);bank.sampleAll(down,2,1017);assert(events.size()==2&&events[0].mask==3&&events[1].mask==3);assert(events[0].edge==1000&&events[0].held==17);
 down[0]=false;bank.sampleAll(down,2,1110);down[0]=true;bank.sampleAll(down,2,1114);down[0]=false;bank.sampleAll(down,2,1117);bank.sampleAll(down,2,1130);assert(events.back().name=="release"&&events.back().mask==2&&events.back().held==117);
 OrientationInput orientation;MotionConfig motion;int changes=0;
 for(unsigned t=0;t<=480;t+=80)if(orientation.sample(0,1000,0,t,motion)==0)++changes;assert(changes==1);
 for(unsigned t=560;t<1000;t+=80)assert(orientation.sample(1,1,1000,t,motion)==-1);
 assert(orientation.current()==0);for(unsigned t=1040;t<=1520;t+=80)if(orientation.sample(1000,0,0,t,motion)==90)++changes;assert(changes==2);
 c=parse(R"({"tap_window_ms":421})");assert(MotionConfig::merge(c.as<JsonObjectConst>(),motion));
 Core core;TimeBackend hw;TimeSync sync(core,hw);sync.poll(0,false);assert(hw.starts==0);sync.poll(100,true);assert(hw.starts==1);
 sync.poll(15100,true);assert(hw.stops==1);sync.poll(75100,true);assert(hw.starts==2);hw.seconds=1800000000;sync.poll(75200,true);assert(hw.stops==2);
 sync.read(out.to<JsonObject>());assert(out["last_sync_unix_s"]==1800000000);sync.poll(80000,true);assert(hw.starts==2);
 c=parse(R"({"settings":{"interval_ms":10}})");assert(sync.invoke("configure",c.as<JsonObjectConst>(),out.to<JsonObject>(),90000));assert(sync.config().interval==21600000);
 assert(!sync.restore(R"({"interval_ms":10})"));assert(sync.config().interval==21600000);
 c=parse(R"({"settings":{"enabled":false}})");assert(!sync.invoke("configure",c.as<JsonObjectConst>(),out.to<JsonObject>(),100000));sync.poll(100000,true);assert(hw.starts==2);
 JsonDocument empty;assert(!sync.invoke("sync",empty.as<JsonObjectConst>(),out.to<JsonObject>(),100100));sync.poll(100100,false);assert(hw.starts==2);sync.poll(100200,true);assert(hw.starts==3);
 PresentationState views;uint32_t one,two;assert(!views.create(7,true,true,one));assert(!views.activate(one,true,7));auto old=views.stamp(one);assert(views.current(old));
 assert(!views.create(7,true,true,two));assert(!views.activate(two,true,7));assert(!views.current(old));assert(!views.revise(one,7));assert(views.active()==two); // even the SAME application's background view cannot acquire foreground by update
 auto second=views.stamp(two);views.completed(old);assert(views.presented().view==one&&views.active()==two); // committed old frame completes, never interrupted
 views.completed(second);assert(views.presented().view==two);uint32_t disposed=0;assert(!views.back(7,disposed)&&disposed==two&&views.active()==one);assert(!views.current(old));
 assert(views.revise(one,8));auto previous=views.stamp(one);assert(views.revise(one,7,previous.revision+1));assert(views.current(previous));
 assert(!views.close(one,7));assert(!views.active());
 PresentationState repeated;assert(!repeated.create(1,true,true,one));assert(!repeated.create(1,true,true,two));assert(!repeated.activate(one,true,1));assert(!repeated.activate(two,true,1));assert(!repeated.activate(one,true,1));assert(!repeated.back(1,disposed)&&!disposed&&repeated.active()==two);assert(!repeated.back(1,disposed)&&disposed==two&&repeated.active()==one&&repeated.get(one));
 static unsigned effects=0;core.add({"/effect","write","",[](const char*,JsonObjectConst a,JsonObject)->const char*{assert(!a.containsKey("expected_boot_id"));++effects;return nullptr;}});
 auto envelope=parse(R"({"path":"/effect","method":"write","args":{"expected_boot_id":"old"}})");invokeEnvelope(core,envelope.as<JsonObjectConst>(),out.to<JsonObject>(),"new",10);assert(out["ok"]==false&&out["error"]=="boot_changed"&&effects==0);
 envelope["args"]["expected_boot_id"]="new";invokeEnvelope(core,envelope.as<JsonObjectConst>(),out.to<JsonObject>(),"new",11);assert(out["ok"]==true&&effects==1);
 envelope["expected_boot_id"]="old";invokeEnvelope(core,envelope.as<JsonObjectConst>(),out.to<JsonObject>(),"new",12);assert(out["ok"]==false&&effects==1);
 std::cout<<"Button bounce/multiclick/hold/restore/wrap, orientation hysteresis, NTP offline/retry/schedule passed\n";
}
