#include "drivers/ft6336/Ft6336Input.h"
#include <cassert>
#include <thread>
#include <chrono>
#include <mutex>
#include <iostream>
using namespace notesos;using namespace notesos::drivers;
static std::thread task;static std::atomic<bool>launchAllowed{true};static std::atomic<uint32_t>page{7};
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
 std::cout<<"Touch capture preserves short input while UI loop is blocked, epochs, owned configuration and native cleanup passed\n";
}
