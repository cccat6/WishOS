#include "drivers/ft6336/Ft6336Input.h"
#include <cassert>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <iostream>
using namespace notesos;using namespace notesos::drivers;
static std::thread task;static std::atomic<bool>launchAllowed{true};static std::atomic<uint32_t>page{7};
static std::mutex contextMutex;static InputPresentation visible{13,4,6};
static InputPresentation captureContext(){std::lock_guard<std::mutex>lock(contextMutex);return visible;}
static void replaceContext(InputPresentation next){std::lock_guard<std::mutex>lock(contextMutex);visible=next;}
static uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static bool launch(void(*work)(void*),void*p){if(!launchAllowed)return false;if(task.joinable())task.join();task=std::thread([=]{work(p);});return true;}
static void sleep(unsigned ms){std::this_thread::sleep_for(std::chrono::milliseconds(ms));}
struct Bus {
 std::mutex mutex;std::atomic<unsigned>contacts{0};I2cBus io{this,[](void*p){return static_cast<Bus*>(p)->mutex.try_lock();},[](void*p){static_cast<Bus*>(p)->mutex.unlock();},[](void*p,uint8_t address,const uint8_t*tx,size_t tn,uint8_t*rx,size_t n)->const char*{assert(address==0x38&&tn==1&&tx[0]==2&&n==13);memset(rx,0,n);rx[0]=static_cast<Bus*>(p)->contacts.load();if(rx[0]){rx[2]=70;rx[4]=90;}return nullptr;}};
};
int main(){Core core;core.clockSource(now);Bus bus;Ft6336 backend(bus.io,0x38,{240,320,true,false,true});Ft6336Input input(core,backend,{launch,6144},[](){return page.load();},sleep);core.add({"/dev/touch","configure_stream","",nullptr,nullptr,"",&input,[](void*p,const char*,JsonObjectConst a,JsonObject o){return static_cast<Ft6336Input*>(p)->configure(a,o);}});JsonDocument a,o;auto owner=core.acquireOwner();a["enabled"]=true;a["interval_ms"]=5;assert(!core.invoke("/dev/touch","configure_stream",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));sleep(20);input.poll();auto cursor=core.sequence();
 // No event-loop polling occurs during this physical press and release.
 bus.contacts=1;sleep(30);bus.contacts=0;sleep(30);assert(!core.invoke("/dev/touch","configure_stream",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));input.poll();JsonDocument events;core.events(cursor,8,events.to<JsonObject>());bool down=false,up=false;uint64_t pressed=0;for(auto e:events["events"].as<JsonArrayConst>())if(e["event"]=="input"){assert(e["target_owner"]==owner&&e["data"]["presentation_id"]==7);if(e["data"]["count"]==1){down=true;pressed=e["at_ms"];}else if(down){up=true;assert(e["at_ms"].as<uint64_t>()>=pressed+15);}}assert(down&&up);
 // Rejected settings preserve the existing acquisition. Foreign instances
 // cannot stop a live stream, and release never strands its native resources.
 a["interval_ms"]=1;assert(input.configure(a.as<JsonObjectConst>(),o.to<JsonObject>()));input.read(o.to<JsonObject>());assert(o["input_stream"]["interval_ms"]==5);a.clear();a["enabled"]=false;auto other=core.acquireOwner();assert(!strcmp(core.invoke("/dev/touch","configure_stream",a.as<JsonObjectConst>(),o.to<JsonObject>(),other),"touch_input_owned_by_other"));
 core.releaseOwner(owner);sleep(15);input.poll();if(task.joinable())task.join();input.poll();assert(core.resources().promised()==0);
 // Fresh configuration/instance invalidates queued samples before publication.
 a.clear();a["enabled"]=true;a["interval_ms"]=5;assert(!core.invoke("/dev/touch","configure_stream",a.as<JsonObjectConst>(),o.to<JsonObject>(),other));bus.contacts=1;sleep(15);page=9;bus.contacts=0;a["interval_ms"]=10;assert(!core.invoke("/dev/touch","configure_stream",a.as<JsonObjectConst>(),o.to<JsonObject>(),other));sleep(25);cursor=core.sequence();input.poll();core.events(cursor,8,events.to<JsonObject>());for(auto e:events["events"].as<JsonArrayConst>())if(e["event"]=="input")assert(e["data"]["presentation_id"]==9&&e["data"]["count"]==0);core.releaseOwner(other);sleep(20);input.poll();if(task.joinable())task.join();input.poll();assert(core.resources().promised()==0);
 // Native launch failure is a clean admission refusal.
 launchAllowed=false;a.clear();a["enabled"]=true;assert(!strcmp(input.configure(a.as<JsonObjectConst>(),o.to<JsonObject>()),"native_task_allocation_failed"));assert(core.resources().promised()==0);
 // Full capture context is one host snapshot. Even unchanged coordinates must
 // report a same-view revision or activation change, independently of publish.
 launchAllowed=true;
 {Ft6336Input stamped(core,backend,{launch,6144},captureContext,sleep);a.clear();a["enabled"]=true;a["interval_ms"]=5;bus.contacts=1;cursor=core.sequence();assert(!stamped.configure(a.as<JsonObjectConst>(),o.to<JsonObject>()));
  auto waitStamp=[&](unsigned revision,unsigned activation){auto deadline=now()+2000;JsonDocument state;do{sleep(10);stamped.poll();state.clear();stamped.read(state.to<JsonObject>());if(state["input_stream"]["valid"]==true&&state["input_stream"]["presentation_revision"]==revision&&state["input_stream"]["presentation_activation"]==activation)return;}while(now()<deadline);assert(false&&"touch sample did not reach the expected presentation");};
  waitStamp(4,6);replaceContext({13,5,6});waitStamp(5,6);replaceContext({13,5,7});waitStamp(5,7);bus.contacts=0;stamped.poll();core.events(cursor,16,events.to<JsonObject>());
  bool original=false,revised=false,activated=false;for(auto e:events["events"].as<JsonArrayConst>())if(e["event"]=="input"){auto d=e["data"];assert(d["presentation_id"]==13);unsigned revision=d["presentation_revision"]|0u,activation=d["presentation_activation"]|0u;if(revision==4&&activation==6)original=true;else if(revision==5&&activation==6)revised=true;else if(revision==5&&activation==7)activated=true;else assert(false);}
  assert(original&&revised&&activated);stamped.read(o.to<JsonObject>());assert(o["input_stream"]["presentation_revision"]==5&&o["input_stream"]["presentation_activation"]==7);
  a["enabled"]=false;assert(!stamped.configure(a.as<JsonObjectConst>(),o.to<JsonObject>()));sleep(20);if(task.joinable())task.join();stamped.poll();assert(core.resources().promised()==0);
 }
 // A slow physical transaction from an old configuration cannot publish
 // failure or contacts into a replacement stream. Configure/owner release
 // finish while I/O is still blocked, so no device wait holds the state lock.
 for(bool staleError:{false,true})for(bool replaceOwner:{false,true}){
  struct BlockingBus{std::mutex physical,gate;std::condition_variable changed;bool waiting=false,released=false;unsigned reads=0;bool failOld;
   I2cBus io{this,[](void*p){return static_cast<BlockingBus*>(p)->physical.try_lock();},[](void*p){static_cast<BlockingBus*>(p)->physical.unlock();},[](void*p,uint8_t,const uint8_t*,size_t,uint8_t*out,size_t size)->const char*{auto&s=*static_cast<BlockingBus*>(p);std::unique_lock<std::mutex>lock(s.gate);bool first=++s.reads==1;if(first){s.waiting=true;s.changed.notify_all();s.changed.wait(lock,[&]{return s.released;});}memset(out,0,size);if(first&&s.failOld)return "old_sample_io_error";if(first){out[0]=1;out[2]=70;out[4]=90;}s.changed.notify_all();return nullptr;}};
  } blocked;blocked.failOld=staleError;Core c;c.clockSource(now);Ft6336 device(blocked.io);Ft6336Input stream(c,device,{launch,6144},[](){return page.load();},sleep);c.add({"/dev/touch","configure_stream","",nullptr,nullptr,"",&stream,[](void*p,const char*,JsonObjectConst a,JsonObject o){return static_cast<Ft6336Input*>(p)->configure(a,o);}});
  auto first=c.acquireOwner(),second=c.acquireOwner();JsonDocument args,state,events;args["enabled"]=true;args["interval_ms"]=5;assert(!c.invoke("/dev/touch","configure_stream",args.as<JsonObjectConst>(),state.to<JsonObject>(),first));
  {std::unique_lock<std::mutex>lock(blocked.gate);assert(blocked.changed.wait_for(lock,std::chrono::seconds(2),[&]{return blocked.waiting;}));}
  args["interval_ms"]=1;assert(!strcmp(c.invoke("/dev/touch","configure_stream",args.as<JsonObjectConst>(),state.to<JsonObject>(),first),"touch_input_interval"));stream.read(state.to<JsonObject>());assert(state["input_stream"]["enabled"]==true&&state["input_stream"]["owner"]==first&&state["input_stream"]["interval_ms"]==5);
  if(replaceOwner)c.releaseOwner(first);auto current=replaceOwner?second:first;args["interval_ms"]=10;assert(!c.invoke("/dev/touch","configure_stream",args.as<JsonObjectConst>(),state.to<JsonObject>(),current));assert(state["input_stream"]["enabled"]==true&&state["input_stream"]["owner"]==current);
  {std::lock_guard<std::mutex>lock(blocked.gate);blocked.released=true;}blocked.changed.notify_all();{std::unique_lock<std::mutex>lock(blocked.gate);assert(blocked.changed.wait_for(lock,std::chrono::seconds(2),[&]{return blocked.reads>=2;}));}
  sleep(15);stream.read(state.to<JsonObject>());assert(state["input_stream"]["enabled"]==true&&state["input_stream"]["owner"]==current&&state["input_stream"]["interval_ms"]==10&&state["input_stream"]["error"]==""&&state["input_stream"]["sample"]["count"]==0);stream.poll();c.events(0,16,events.to<JsonObject>());for(auto event:events["events"].as<JsonArrayConst>()){assert(event["event"]!="failed");if(event["event"]=="input")assert(event["target_owner"]==current&&event["data"]["count"]==0);}
  args["enabled"]=false;assert(!c.invoke("/dev/touch","configure_stream",args.as<JsonObjectConst>(),state.to<JsonObject>(),current));if(task.joinable())task.join();stream.poll();assert(c.resources().promised()==0);
  // A rejected fresh launch preserves the complete previous stream settings.
  JsonDocument before,after;stream.read(before.to<JsonObject>());launchAllowed=false;args["enabled"]=true;args["interval_ms"]=20;assert(!strcmp(c.invoke("/dev/touch","configure_stream",args.as<JsonObjectConst>(),state.to<JsonObject>(),current),"native_task_allocation_failed"));launchAllowed=true;stream.read(after.to<JsonObject>());assert(before==after&&c.resources().promised()==0);
 }
 // A single failed I2C transaction loses one sample, not the entire native
 // input stream. Sustained failures still notify the application so it can
 // select its existing fallback path.
 {
  struct TransientBus{std::mutex physical;int remaining=1;std::atomic<unsigned>contacts{0};
   I2cBus io{this,[](void*p){return static_cast<TransientBus*>(p)->physical.try_lock();},[](void*p){static_cast<TransientBus*>(p)->physical.unlock();},[](void*p,uint8_t,const uint8_t*,size_t,uint8_t*out,size_t size)->const char*{auto&s=*static_cast<TransientBus*>(p);if(s.remaining){--s.remaining;return "transient_i2c_timeout";}memset(out,0,size);out[0]=s.contacts.load();if(out[0]){out[2]=70;out[4]=90;}return nullptr;}};
  } transient;
  Core c;c.clockSource(now);Ft6336 device(transient.io);Ft6336Input stream(c,device,{launch,6144},captureContext,sleep);
  auto owner=c.acquireOwner();JsonDocument args,state,events;args["enabled"]=true;args["interval_ms"]=5;auto cursor=c.sequence();assert(!stream.configure(args.as<JsonObjectConst>(),state.to<JsonObject>()));
  auto deadline=now()+2000;do{sleep(10);stream.poll();stream.read(state.to<JsonObject>());}while(now()<deadline&&!(state["input_stream"]["sample_errors"]==1&&state["input_stream"]["consecutive_errors"]==0&&state["input_stream"]["valid"]==true));assert(state["input_stream"]["enabled"]==true&&state["input_stream"]["error"]==""&&state["input_stream"]["sample_errors"]==1&&state["input_stream"]["consecutive_errors"]==0);
  c.events(cursor,16,events.to<JsonObject>());for(auto event:events["events"].as<JsonArrayConst>())assert(event["event"]!="failed");
  transient.contacts=1;sleep(20);stream.poll();c.events(cursor,16,events.to<JsonObject>());bool recovered=false;for(auto event:events["events"].as<JsonArrayConst>())if(event["event"]=="input"&&event["data"]["count"]==1)recovered=true;assert(recovered);
  {std::lock_guard<std::mutex>lock(transient.physical);transient.remaining=3;}cursor=c.sequence();deadline=now()+2000;do{sleep(10);stream.poll();stream.read(state.to<JsonObject>());}while(now()<deadline&&state["input_stream"]["enabled"]==true);assert(state["input_stream"]["enabled"]==false&&state["input_stream"]["error"]=="transient_i2c_timeout"&&state["input_stream"]["consecutive_errors"]==3&&state["input_stream"]["sample_errors"]==4);
  unsigned failures=0;auto through=c.sequence();while(cursor<through){events.clear();c.events(cursor,8,events.to<JsonObject>());assert(events["gap"]==false);auto next=events["cursor"].as<uint32_t>();assert(next>cursor);for(auto event:events["events"].as<JsonArrayConst>())if(event["event"]=="failed")++failures;cursor=next;}assert(failures==1);
  if(task.joinable())task.join();stream.poll();c.releaseOwner(owner);assert(c.resources().promised()==0);
 }
 std::cout<<"Touch capture preserves short input while UI loop is blocked, epochs, owned configuration and native cleanup passed\n";
}
