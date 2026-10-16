#include "NativeOperation.h"
#include <thread>
#include <cassert>
#include <iostream>
using namespace notesos;
static NativeOperation*activeOperation=nullptr;static std::thread task;static bool launchFails=false;
static bool launch(void(*run)(void*),void*context){if(launchFails)return false;task=std::thread(run,context);return true;}
struct Work {std::atomic<bool>entered{false},finish{false};unsigned published=0;};
int main(){Core core;Work state;NativeOperation op(core,"/storage/device",{launch,8192},&state,[](void*p,const std::atomic<bool>&cancel)->const char*{auto&s=*static_cast<Work*>(p);s.entered=true;while(!s.finish&&!cancel)std::this_thread::yield();return cancel?"cancelled":nullptr;},[](void*p){++static_cast<Work*>(p)->published;});
 activeOperation=&op;JsonDocument args,out;uint32_t owner=core.acquireOwner();
 core.add({"/storage/device","prepare,read,cancel","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{return !strcmp(m,"prepare")?activeOperation->start(m,o):activeOperation->cancel(a);}});
 launchFails=true;assert(op.start("prepare",out.to<JsonObject>())&&core.resources().promised()==0&&!op.active());launchFails=false;
 assert(!op.start("prepare",out.to<JsonObject>()));auto id=out["operation_id"].as<uint32_t>();
 while(!state.entered)std::this_thread::yield();assert(op.active()&&state.published==0);assert(op.start("prepare",out.to<JsonObject>()));
 // The event loop and independently scheduled work continue through a stalled SDK.
 args["delay_ms"]=1;core.timer(args.as<JsonObjectConst>(),out.to<JsonObject>());core.clock(2);core.tick();assert(core.sequence()==1);
 args.clear();args["operation_id"]=id;assert(!op.cancel(args.as<JsonObjectConst>()));task.join();assert(op.active());op.poll();assert(!op.active()&&state.published==1&&core.resources().promised()==0);
 op.read(out.to<JsonObject>());assert(out["state"]=="failed"&&out["error"]=="cancelled");
 state.finish=true;assert(!op.start("prepare",out.to<JsonObject>()));task.join();op.poll();assert(state.published==2);op.read(out.to<JsonObject>());assert(out["state"]=="completed");
 state.finish=false;state.entered=false;assert(!core.invoke("/storage/device","prepare",JsonObjectConst(),out.to<JsonObject>(),owner));args["operation_id"]=out["operation_id"];assert(core.invoke("/storage/device","cancel",args.as<JsonObjectConst>(),out.to<JsonObject>(),core.acquireOwner()));core.releaseOwner(owner);assert(core.resources().promised()==8192);task.join();op.poll();assert(!op.active()&&core.resources().promised()==0);op.read(out.to<JsonObject>());assert(out["error"]=="cancelled");
 // Instance-bound retained executors do not require process-global driver
 // pointers, and preserve the same work/settlement boundary.
 {Work other;other.finish=true;NativeOperation isolated(core,"/other",{nullptr,0,[](void*p,void(*run)(void*),void*job){assert(static_cast<Work*>(p)->finish);task=std::thread(run,job);return true;}},&other,[](void*p,const std::atomic<bool>&)->const char*{static_cast<Work*>(p)->entered=true;return nullptr;},[](void*p){++static_cast<Work*>(p)->published;});assert(!isolated.start("start",out.to<JsonObject>()));task.join();assert(other.entered&&other.published==0);isolated.poll();assert(other.published==1);}
 std::cout<<"Native worker admission, nonblocking event loop, cooperative cancel, publication and resource cleanup passed\n";
}
