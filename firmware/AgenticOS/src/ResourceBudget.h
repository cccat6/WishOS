#pragma once
#include <ArduinoJson.h>
#include <atomic>
#include <mutex>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
namespace notesos {
// Admission and allocation use the same heap. External RAM cannot satisfy an
// internal/DMA reservation. A VM stays on its selected heap for its lifetime.
class ResourceBudget {
public:
 enum class Region : uint8_t { Internal, External, Auto };
 struct Heap {size_t(*capacity)()=nullptr;size_t(*available)()=nullptr;size_t(*largest)()=nullptr;void*(*resize)(void*,size_t)=nullptr;};
private:
 struct Slot {std::atomic<uint32_t>id{0};uint32_t owner=0;size_t limit=0;Region region=Region::Internal;bool elastic=false;std::atomic<size_t>used{0};} slots_[16];
 mutable std::recursive_mutex mutex_;
 Heap heaps_[2];size_t reserves_[2]{};
 struct alignas(max_align_t) Allocation {size_t size;Region region;};
 std::atomic<size_t> transient_[2]{};
 size_t configuredLimit_=0,defaultVm_=32768;Region applicationRegion_=Region::Auto;uint32_t next_=1,failures_=0;
 Slot*find(uint32_t id){for(auto&s:slots_)if(id&&s.id==id)return &s;return nullptr;}
 const Slot*find(uint32_t id)const{for(auto&s:slots_)if(id&&s.id==id)return &s;return nullptr;}
 static unsigned index(Region r){return unsigned(r);}
 bool present(Region r)const{const auto&h=heaps_[index(r)];return h.capacity?h.capacity()>0:r==Region::Internal;}
 size_t freeBytes(Region r)const{const auto&h=heaps_[index(r)];return h.available?h.available():r==Region::Internal?UINT32_MAX:0;}
 size_t pendingIn(Region r)const{size_t n=0;for(auto&s:slots_)if(s.id&&!s.elastic&&s.region==r){size_t used=s.used.load();if(used<s.limit)n+=s.limit-used;}return n;}
 bool fits(size_t bytes,Region r)const{if(!present(r))return false;size_t available=freeBytes(r),waiting=pendingIn(r),reserve=reserves_[index(r)];return available>=reserve&&waiting<=available-reserve&&bytes<=available-reserve-waiting;}
public:
 static const char*name(Region r){return r==Region::External?"external":r==Region::Internal?"internal":"auto";}
 // Native composition installs heaps before starting applications/services.
 void heap(Region r,Heap h){std::lock_guard<std::recursive_mutex> lock(mutex_);heaps_[index(r)]=h;}
 size_t limit()const{std::lock_guard<std::recursive_mutex> lock(mutex_);if(configuredLimit_)return configuredLimit_;uint64_t total=0;for(auto r:{Region::Internal,Region::External})if(present(r)){const auto&h=heaps_[index(r)];total+=h.capacity?h.capacity():UINT32_MAX;}return total>UINT32_MAX?UINT32_MAX:size_t(total);}
 size_t defaultVmLimit()const{std::lock_guard<std::recursive_mutex> lock(mutex_);return defaultVm_;}
 size_t promised()const{std::lock_guard<std::recursive_mutex> lock(mutex_);size_t n=transient_[0].load()+transient_[1].load();for(auto&s:slots_)if(s.id)n+=s.elastic?s.used.load():s.limit;return n;}
 size_t pending()const{std::lock_guard<std::recursive_mutex> lock(mutex_);return pendingIn(Region::Internal)+pendingIn(Region::External);}
 size_t allocatableHeap(Region r=Region::Internal)const{std::lock_guard<std::recursive_mutex> lock(mutex_);if(r==Region::Auto){size_t a=allocatableHeap(Region::Internal),b=allocatableHeap(Region::External);return a>b?a:b;}size_t free=freeBytes(r),waiting=pendingIn(r);return free>waiting?free-waiting:0;}
 bool canAllocate(size_t bytes,Region r=Region::Internal)const{std::lock_guard<std::recursive_mutex> lock(mutex_);return r==Region::Auto?(fits(bytes,Region::External)||fits(bytes,Region::Internal)):fits(bytes,r);}
 uint32_t acquire(size_t bytes,uint32_t owner,Region region=Region::Internal,bool elastic=false){
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  size_t required=elastic?(bytes<16384?bytes:16384):bytes;
  size_t total=promised(),maximum=limit();if(!bytes||bytes>maximum||total>maximum-required||!next_){++failures_;return 0;}
  if(region==Region::Auto)region=fits(required,Region::External)?Region::External:Region::Internal;
  if(!fits(required,region)){++failures_;return 0;}
  for(auto&s:slots_)if(!s.id){s.owner=owner;s.limit=bytes;s.region=region;s.elastic=elastic;s.used=0;s.id=next_++;return s.id;}++failures_;return 0;
 }
 uint32_t acquireApplication(size_t bytes,uint32_t owner){std::lock_guard<std::recursive_mutex> lock(mutex_);return acquire(bytes,owner,applicationRegion_,true);}
 // Demand-allocated caches have no speculative full-size reservation. They are
 // charged for actual blocks and observe the same heap choice/reserve policy.
 void*allocate(size_t bytes,Region region=Region::Auto){std::lock_guard<std::recursive_mutex> lock(mutex_);
  if(bytes>SIZE_MAX-sizeof(Allocation))return nullptr;size_t n=bytes+sizeof(Allocation),total=promised(),maximum=limit();
  if(n>maximum||total>maximum-n)return nullptr;
  if(region==Region::Auto)region=fits(n,Region::External)?Region::External:Region::Internal;if(!fits(n,region))return nullptr;
  auto fn=heaps_[index(region)].resize;auto p=static_cast<Allocation*>(fn?fn(nullptr,n):malloc(n));if(!p)return nullptr;
  p->size=n;p->region=region;transient_[index(region)]+=n;return p+1;
 }
 void freeAllocation(void*ptr){std::lock_guard<std::recursive_mutex> lock(mutex_);if(!ptr)return;auto p=static_cast<Allocation*>(ptr)-1;auto r=p->region;transient_[index(r)]-=p->size;auto fn=heaps_[index(r)].resize;if(fn)fn(p,0);else free(p);}
 const char*allocationRegion(uint32_t id)const{std::lock_guard<std::recursive_mutex> lock(mutex_);auto s=find(id);return s?name(s->region):"unallocated";}
 void*resize(uint32_t id,void*ptr,size_t size,size_t previous=0){
  std::lock_guard<std::recursive_mutex> lock(mutex_);auto s=find(id);if(!s)return nullptr;
  if(s->elastic&&size>previous){size_t growth=size-previous,maximum=limit(),charged=promised();if(growth>maximum||charged>maximum-growth||!fits(growth,s->region)){++failures_;return nullptr;}}
  auto fn=heaps_[index(s->region)].resize;void*next=nullptr;if(fn)next=fn(ptr,size);else if(size)next=realloc(ptr,size);else free(ptr);
  if(s->elastic&&(!size||next))s->used=s->used.load()-previous+size;return next;
 }
 void used(uint32_t id,size_t bytes){std::lock_guard<std::recursive_mutex> lock(mutex_);auto s=find(id);if(s)s->used=bytes;}
 // A completed native operation may retain its result but no longer needs its
 // task/transport headroom. Keep the remaining allocation charged until release.
 void releaseUnused(uint32_t id){std::lock_guard<std::recursive_mutex> lock(mutex_);auto s=find(id);if(s&&!s->elastic&&s->used<s->limit)s->limit=s->used.load();}
 void release(uint32_t id){std::lock_guard<std::recursive_mutex> lock(mutex_);auto s=find(id);if(s){s->id=0;s->used=0;s->owner=0;s->limit=0;}}
 const char*configure(JsonObjectConst a){std::lock_guard<std::recursive_mutex> lock(mutex_);
  for(auto f:a)if(strcmp(f.key().c_str(),"memory_limit_bytes")&&strcmp(f.key().c_str(),"heap_reserve_bytes")&&strcmp(f.key().c_str(),"external_heap_reserve_bytes")&&strcmp(f.key().c_str(),"default_vm_memory_bytes")&&strcmp(f.key().c_str(),"application_memory"))return "unknown_resource_setting";
  for(auto key:{"heap_reserve_bytes","external_heap_reserve_bytes","default_vm_memory_bytes"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "resource_type";
  if(a.containsKey("memory_limit_bytes")&&!a["memory_limit_bytes"].isNull()&&!a["memory_limit_bytes"].is<uint32_t>())return "resource_type";
  if(a.containsKey("default_vm_memory_bytes")&&a["default_vm_memory_bytes"].as<uint32_t>()<16384)return "vm_memory_policy_range";
  Region region=applicationRegion_;
  if(a.containsKey("application_memory")){const char*v=a["application_memory"]|"";if(!strcmp(v,"auto"))region=Region::Auto;else if(!strcmp(v,"internal"))region=Region::Internal;else if(!strcmp(v,"external"))region=Region::External;else return "memory_region";if(region==Region::External&&!present(region))return "memory_region_unavailable";}
  size_t maximum=a.containsKey("memory_limit_bytes")?(a["memory_limit_bytes"].isNull()?0:a["memory_limit_bytes"].as<uint32_t>()):configuredLimit_;
  if(a.containsKey("memory_limit_bytes")&&!a["memory_limit_bytes"].isNull()&&!maximum)return "resource_budget_range";
  if(maximum&&maximum<promised())return "resource_budget_in_use";
  configuredLimit_=maximum;reserves_[0]=a["heap_reserve_bytes"]|uint32_t(reserves_[0]);reserves_[1]=a["external_heap_reserve_bytes"]|uint32_t(reserves_[1]);defaultVm_=a["default_vm_memory_bytes"]|uint32_t(defaultVm_);applicationRegion_=region;return nullptr;
 }
 void read(JsonObject o)const{std::lock_guard<std::recursive_mutex> lock(mutex_);
  o["configuration_scope"]="system";o["instance_default_apply_time"]="next_instance";o["default_vm_memory_bytes"]=defaultVm_;o["memory_limit_bytes"]=limit();
  if(configuredLimit_)o["configured_memory_limit_bytes"]=configuredLimit_;else o["configured_memory_limit_bytes"]=nullptr;
  size_t reserved=0,application=0;for(auto&s:slots_)if(s.id){if(s.elastic)application+=s.used.load();else reserved+=s.limit;}
  o["application_memory"]=name(applicationRegion_);o["heap_reserve_bytes"]=reserves_[0];o["external_heap_reserve_bytes"]=reserves_[1];o["managed_bytes"]=promised();o["application_allocated_bytes"]=application;o["cache_allocated_bytes"]=transient_[0].load()+transient_[1].load();o["reserved_bytes"]=reserved;o["unallocated_reserved_bytes"]=pending();o["admission_failures"]=failures_;
  if(heaps_[0].available)o["heap_free_bytes"]=heaps_[0].available();else o["heap_free_bytes"]=nullptr;o["scheduler"]="native_rtos_with_bounded_lua";
  auto pools=o["heaps"].to<JsonArray>();for(auto r:{Region::Internal,Region::External})if(present(r)){const auto&h=heaps_[index(r)];auto p=pools.add<JsonObject>();p["id"]=name(r);if(h.capacity)p["capacity_bytes"]=h.capacity();else p["capacity_bytes"]=nullptr;if(h.available)p["free_bytes"]=h.available();else p["free_bytes"]=nullptr;if(h.largest)p["largest_free_block_bytes"]=h.largest();else p["largest_free_block_bytes"]=nullptr;p["reserve_bytes"]=reserves_[index(r)];p["unallocated_reserved_bytes"]=pendingIn(r);p["cache_allocated_bytes"]=transient_[index(r)].load();}
  auto reservations=o["reservations"].to<JsonArray>();for(auto&s:slots_)if(s.id){auto p=reservations.add<JsonObject>();p["id"]=s.id.load();p["owner"]=s.owner;p["region"]=name(s.region);p["allocation_mode"]=s.elastic?"elastic_ceiling":"reservation";p["limit_bytes"]=s.limit;p["used_bytes"]=s.used.load();}
 }
};
}
