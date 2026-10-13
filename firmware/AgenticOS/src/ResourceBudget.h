#pragma once
#include <ArduinoJson.h>
#include <atomic>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace notesos {
// Reservations include future VM growth. Actual heap telemetry already includes materialized bytes.
class ResourceBudget {
 struct Slot{std::atomic<uint32_t>id{0};uint32_t owner=0;size_t limit=0;std::atomic<size_t>used{0};};Slot slots_[16];
 size_t limit_=262144,reserve_=20000,defaultVm_=32768;uint32_t next_=1,failures_=0;size_t(*available_)()=nullptr;
 Slot*find(uint32_t id){for(auto&s:slots_)if(s.id==id&&id)return &s;return nullptr;}
public:
 size_t limit()const{return limit_;}
 size_t defaultVmLimit()const{return defaultVm_;}
 void heapProbe(size_t(*fn)()){available_=fn;}
 size_t promised()const{size_t n=0;for(auto&s:slots_)if(s.id)n+=s.limit;return n;}
 size_t pending()const{size_t n=0;for(auto&s:slots_)if(s.id){size_t used=s.used.load();if(used<s.limit)n+=s.limit-used;}return n;}
 size_t allocatableHeap()const{size_t free=available_?available_():SIZE_MAX,waiting=pending();return free>waiting?free-waiting:0;}
 bool canAllocate(size_t n)const{auto free=allocatableHeap();return free>=reserve_&&n<=free-reserve_;}
 uint32_t acquire(size_t bytes,uint32_t owner){size_t total=promised(),waiting=pending(),free=available_?available_():SIZE_MAX;
  if(!bytes||bytes>limit_||total>limit_-bytes||free<reserve_||waiting>free-reserve_||bytes>free-reserve_-waiting||!next_){++failures_;return 0;}
  for(auto&s:slots_)if(!s.id){s.owner=owner;s.limit=bytes;s.used=0;s.id=next_++;return s.id;}++failures_;return 0;
 }
 void used(uint32_t id,size_t n){auto s=find(id);if(s)s->used=n;}
 void release(uint32_t id){auto s=find(id);if(s){s->id=0;s->used=0;s->owner=0;s->limit=0;}}
 const char*configure(JsonObjectConst a){for(auto field:a)if(strcmp(field.key().c_str(),"memory_limit_bytes")&&strcmp(field.key().c_str(),"heap_reserve_bytes")&&strcmp(field.key().c_str(),"default_vm_memory_bytes"))return "unknown_resource_setting";
  if((a.containsKey("memory_limit_bytes")&&!a["memory_limit_bytes"].is<uint32_t>())||(a.containsKey("heap_reserve_bytes")&&!a["heap_reserve_bytes"].is<uint32_t>()))return "resource_type";
  if(a.containsKey("default_vm_memory_bytes")&&(!a["default_vm_memory_bytes"].is<uint32_t>()||a["default_vm_memory_bytes"].as<uint32_t>()<16384))return "vm_memory_policy_range";
  size_t defaultVm=a["default_vm_memory_bytes"]|uint32_t(defaultVm_);
  size_t limit=a["memory_limit_bytes"]|uint32_t(limit_),reserve=a["heap_reserve_bytes"]|uint32_t(reserve_);if(!limit||limit<promised())return "resource_budget_in_use";limit_=limit;reserve_=reserve;defaultVm_=defaultVm;return nullptr;
 }
 void read(JsonObject o)const{o["configuration_scope"]="system";o["instance_default_apply_time"]="next_instance";o["default_vm_memory_bytes"]=defaultVm_;o["memory_limit_bytes"]=limit_;o["heap_reserve_bytes"]=reserve_;o["reserved_bytes"]=promised();o["unallocated_reserved_bytes"]=pending();o["admission_failures"]=failures_;if(available_)o["heap_free_bytes"]=available_();else o["heap_free_bytes"]=nullptr;o["scheduler"]="native_rtos_with_bounded_lua";auto a=o["reservations"].to<JsonArray>();for(auto&s:slots_)if(s.id){auto r=a.add<JsonObject>();r["id"]=s.id.load();r["owner"]=s.owner;r["limit_bytes"]=s.limit;r["used_bytes"]=s.used.load();}}
};
}
