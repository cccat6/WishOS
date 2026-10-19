#include "LuaRuntime.h"
#include "Workers.h"
#include <vector>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <atomic>
using namespace notesos;
struct TestHeap {
 struct alignas(max_align_t) Allocation{size_t size;};
 size_t capacity=0,used=0,largest=SIZE_MAX,allocations=0,failFrom=SIZE_MAX;
 void*resize(void*p,size_t n){
  auto old=p?static_cast<Allocation*>(p)-1:nullptr;size_t before=old?old->size:0;
  if(!n){if(old){used-=before;free(old);}return nullptr;}
  if(n>capacity-used+before||n>largest||allocations>=failFrom)return nullptr;
  auto next=static_cast<Allocation*>(realloc(old,n+sizeof(Allocation)));if(!next)return nullptr;
  next->size=n;used=used-before+n;++allocations;return next+1;
 }
};
static TestHeap internal,external;
static ResourceBudget::Heap heap(TestHeap&h){
 if(&h==&internal)return {[](){return internal.capacity;},[](){return internal.capacity-internal.used;},[](){return internal.largest;},[](void*p,size_t n){return internal.resize(p,n);}};
 return {[](){return external.capacity;},[](){return external.capacity-external.used;},[](){return external.largest;},[](void*p,size_t n){return external.resize(p,n);}};
}
struct WorkerEntry{void(*run)(void*);void*context;};
static std::vector<WorkerEntry>workQueue;static bool rejectWorkerLaunch=false;static ResourceBudget*workerBudget=nullptr;
static void workerMemoryScenario(bool psram){
 Core core;auto&memory=core.resources();Vfs vfs;internal={};external={};internal.capacity=256*1024;external.capacity=psram?2*1024*1024:0;
 memory.heap(ResourceBudget::Region::Internal,heap(internal));memory.heap(ResourceBudget::Region::External,heap(external));workerBudget=&memory;
 JsonDocument policy;policy["heap_reserve_bytes"]=20000;assert(!memory.configure(policy.as<JsonObjectConst>()));
 Workers workers(core,vfs,{2,[](void(*run)(void*),void*context,unsigned cpu){assert(cpu<2);if(rejectWorkerLaunch)return false;workQueue.push_back({run,context});return true;},nullptr,[](size_t n){return workerBudget->canAllocate(n,ResourceBudget::Region::Internal);},8192});
 JsonDocument args,out;std::string text(1800,'t'),source="function compute(x) return {text=x.text,value=notes.decode(notes.encode({n=7})).n} end --";source+=std::string(2000,' ');args["source"]=source;args["input"]["text"]=text;args["memory_bytes"]=65536;
 uint32_t ids[2];for(unsigned cpu=0;cpu<2;++cpu){args["core"]=cpu;assert(!workers.invoke("submit",args.as<JsonObjectConst>(),out.to<JsonObject>()));ids[cpu]=out["id"];}
 assert(workQueue.size()==2);if(psram)assert(external.used>7000&&internal.used==0);else assert(internal.used>7000&&external.used==0);
 for(auto entry:workQueue)entry.run(entry.context);workQueue.clear();workers.poll();
 for(auto id:ids){JsonDocument query;query["id"]=id;assert(!workers.invoke("read",query.as<JsonObjectConst>(),out.to<JsonObject>()));assert(out["ok"]==true&&out["result"]["text"]==text&&out["result"]["value"]==7&&out["runtime"]["memory_region"]==(psram?"external":"internal"));assert(!workers.invoke("release",query.as<JsonObjectConst>(),out.to<JsonObject>()));}
 assert(memory.promised()==0&&internal.used==0&&external.used==0);
 // Source allocation succeeds, then input metadata OOM rolls back the complete
 // admission without launching a task or retaining source/VM allocations.
 auto&payloadHeap=psram?external:internal;payloadHeap.failFrom=payloadHeap.allocations+1;
 assert(!strcmp(workers.invoke("submit",args.as<JsonObjectConst>(),out.to<JsonObject>()),"memory_low"));payloadHeap.failFrom=SIZE_MAX;assert(workQueue.empty()&&memory.promised()==0&&internal.used==0&&external.used==0);
 rejectWorkerLaunch=true;assert(!strcmp(workers.invoke("submit",args.as<JsonObjectConst>(),out.to<JsonObject>()),"worker_launch_failed"));rejectWorkerLaunch=false;assert(workQueue.empty()&&memory.promised()==0&&internal.used==0&&external.used==0);
 if(psram){internal.used=internal.capacity-4096;assert(!strcmp(workers.invoke("submit",args.as<JsonObjectConst>(),out.to<JsonObject>()),"worker_memory_budget"));assert(workQueue.empty()&&external.used==0&&memory.promised()==0);internal.used=0;}
}
int main(){
 Core c;auto&r=c.resources();internal.capacity=128*1024;r.heap(ResourceBudget::Region::Internal,heap(internal));r.heap(ResourceBudget::Region::External,heap(external));
 assert(!r.reallocate(nullptr,0)&&r.promised()==0&&internal.used==0);
 JsonDocument config,out;config["heap_reserve_bytes"]=20000;assert(!r.configure(config.as<JsonObjectConst>()));assert(r.limit()==internal.capacity);
 {LuaRuntime vm(c);assert(vm.load("function on_start() end",strlen("function on_start() end")));assert(vm.start());vm.describe(out.to<JsonObject>());assert(out["memory_region"]=="internal");}assert(internal.used==0&&r.promised()==0);
 config.clear();config["application_memory"]="external";assert(r.configure(config.as<JsonObjectConst>()));
 external.capacity=4*1024*1024;internal.used=100*1024;assert(r.limit()==internal.capacity+external.capacity);
 {LuaRuntime vm(c,131072);const char*source="function on_start() local text=string.rep('x',20000); assert(#text==20000) end";assert(vm.load(source,strlen(source)));assert(vm.start());vm.describe(out.to<JsonObject>());assert(out["memory_region"]=="external");assert(external.allocations>0&&external.used>0);
  // Plenty of PSRAM cannot satisfy an internal task/DMA reservation.
  assert(!r.acquire(16384,99));auto native=r.acquire(4096,99);assert(native);r.release(native);
 }assert(external.used==0&&internal.used==100*1024&&r.promised()==0);
 internal.used=0;external.used=external.capacity-8192;
 {LuaRuntime vm(c);assert(vm.load("function on_start() end",strlen("function on_start() end")));vm.describe(out.to<JsonObject>());assert(out["memory_region"]=="internal");}assert(internal.used==0);
 assert(!r.configure(config.as<JsonObjectConst>()));{LuaRuntime vm(c);assert(vm.admitted());assert(!vm.load("function on_start() end",strlen("function on_start() end")));vm.describe(out.to<JsonObject>());assert(out["memory_region"]=="external");} // fixed external does not silently migrate
 external.used=0;external.largest=4096;
 {LuaRuntime vm(c,131072);const char*source="function on_start() local text=string.rep('x',20000) end";assert(vm.load(source,strlen(source)));assert(!vm.start());}
 assert(external.used==0&&r.promised()==0);external.largest=SIZE_MAX;
 {LuaRuntime vm(c,131072);assert(vm.load("function on_start() end",strlen("function on_start() end")));assert(vm.start());}
 assert(external.used==0&&r.promised()==0);
 // Subscription metadata is acquired on use, and an empty set releases it;
 // an idle VM no longer pays for every possible module/subscription slot.
 {LuaRuntime vm(c);const char*source="function on_start() notes.subscribe('/s');notes.unsubscribe('/s') end";assert(vm.load(source,strlen(source)));assert(vm.start());vm.describe(out.to<JsonObject>());assert(r.promised()==out["memory_used"].as<size_t>());}assert(external.used==0&&r.promised()==0);
 // Two high ceilings do not preallocate or reserve both ceilings. Actual growth
 // remains bounded by the shared policy and the selected heap's reserve.
 external.capacity=200000;internal.used=100*1024;
 {LuaRuntime first(c,180000),second(c,180000);assert(first.admitted()&&second.admitted());
  assert(first.load("function on_start() end",strlen("function on_start() end")));assert(second.load("function on_start() end",strlen("function on_start() end")));
  assert(r.promised()<360000&&r.pending()==0);assert(first.start()&&second.start());
 }assert(external.used==0&&r.promised()==0);
 // A small elastic client can start below the former fixed 16 KiB estimate;
 // every actual growth still preserves native reserves and rolls back on failure.
 {Core small;auto&budget=small.resources();internal.used=0;internal.capacity=32768;budget.heap(ResourceBudget::Region::Internal,heap(internal));JsonDocument config;config["heap_reserve_bytes"]=20000;assert(!budget.configure(config.as<JsonObjectConst>()));auto id=budget.acquireApplication(32000,1);assert(id);void*memory=budget.resize(id,nullptr,4096);assert(memory&&internal.capacity-internal.used>=20000);assert(!budget.resize(id,memory,16000,4096));assert(internal.used==4096);budget.resize(id,memory,0,4096);budget.release(id);assert(internal.used==0&&budget.promised()==0);}
 internal.capacity=128*1024;
 // Concurrent growth is charged atomically across cores, not admitted twice
 // against the same free bytes. The loser can recover after the winner releases.
 config.clear();config["memory_limit_bytes"]=60000;assert(!r.configure(config.as<JsonObjectConst>()));
 auto a=r.acquireApplication(50000,1),b=r.acquireApplication(50000,2);assert(a&&b);
 std::atomic<unsigned>attempted{0},succeeded{0};
 auto run=[&](uint32_t id){void*p=r.resize(id,nullptr,40000);if(p)++succeeded;++attempted;while(attempted<2)std::this_thread::yield();if(p)r.resize(id,p,0,40000);};
 std::thread left(run,a),right(run,b);left.join();right.join();assert(succeeded==1&&external.used==0);r.release(a);r.release(b);assert(r.promised()==0);
 // System/native threads share one accounting transaction: snapshots stay
 // consistent while app allocations and demand-allocated caches grow/shrink.
 {ResourceBudget shared;std::atomic<unsigned>done{0};
  auto work=[&](unsigned owner){for(unsigned i=0;i<128;++i){auto id=shared.acquireApplication(4096,owner);assert(id);auto p=shared.resize(id,nullptr,1024);assert(p);auto cache=shared.allocate(256);assert(cache);p=shared.resize(id,p,2048,1024);assert(p);cache=shared.reallocate(cache,512);assert(cache);p=shared.resize(id,p,512,2048);assert(p);shared.resize(id,p,0,512);shared.release(id);shared.freeAllocation(cache);}++done;};
  std::thread first(work,1),second(work,2);while(done<2){JsonDocument snapshot;shared.read(snapshot.to<JsonObject>());assert(snapshot["managed_bytes"].as<size_t>()==snapshot["application_allocated_bytes"].as<size_t>()+snapshot["shared_allocated_bytes"].as<size_t>()+snapshot["reserved_bytes"].as<size_t>());}
  first.join();second.join();assert(shared.promised()==0&&shared.pending()==0);
 }
 workerMemoryScenario(false);workerMemoryScenario(true);
 std::cout<<"Internal-only, PSRAM allocation, fixed/auto placement, native reserve and fragmentation recovery passed\n";
}
