#include "LuaRuntime.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
using namespace notesos;
struct TestHeap {
 struct alignas(max_align_t) Allocation{size_t size;};
 size_t capacity=0,used=0,largest=SIZE_MAX,allocations=0;
 void*resize(void*p,size_t n){
  auto old=p?static_cast<Allocation*>(p)-1:nullptr;size_t before=old?old->size:0;
  if(!n){if(old){used-=before;free(old);}return nullptr;}
  if(n>capacity-used+before||n>largest)return nullptr;
  auto next=static_cast<Allocation*>(realloc(old,n+sizeof(Allocation)));if(!next)return nullptr;
  next->size=n;used=used-before+n;++allocations;return next+1;
 }
};
static TestHeap internal,external;
static ResourceBudget::Heap heap(TestHeap&h){
 if(&h==&internal)return {[](){return internal.capacity;},[](){return internal.capacity-internal.used;},[](){return internal.largest;},[](void*p,size_t n){return internal.resize(p,n);}};
 return {[](){return external.capacity;},[](){return external.capacity-external.used;},[](){return external.largest;},[](void*p,size_t n){return external.resize(p,n);}};
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
 std::cout<<"Internal-only, PSRAM allocation, fixed/auto placement, native reserve and fragmentation recovery passed\n";
}
