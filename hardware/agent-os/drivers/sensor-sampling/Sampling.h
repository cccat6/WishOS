#pragma once
#include "Core.h"
namespace notesos::drivers {
struct SampleStep {bool complete=false;const char*error=nullptr;};
class SampleBackend {
public:
 virtual ~SampleBackend()=default;
 virtual void describe(JsonObject)const=0;
 virtual const char*configure(JsonObjectConst,uint64_t)=0;
 virtual void configuration(JsonObject)const{}
 virtual const char*begin(uint64_t)=0;
 virtual SampleStep poll(uint64_t)=0;
 virtual const char*cancel()=0;
 virtual void values(JsonObject)const=0;
 virtual uint32_t timeoutMs()const=0;
};
// This is shared driver code, not a closed sensor taxonomy in the OS. A
// composition registers it at its chosen path and schedules poll(). Applications
// can use ordinary timers/rules for periodic measurements and threshold actions.
class Sampling {
 Core&core_;SampleBackend&backend_;const char*path_;bool pending_=false,cleanup_=false,valid_=false,hook_;
 uint32_t owner_=0,id_=0,next_=1;uint64_t deadline_=0,sampled_=0;const char*error_=nullptr;
 void finish(const char*error,uint64_t now){pending_=false;error_=error;JsonDocument d;d["operation_id"]=id_;if(error)d["error"]=error;else{sampled_=now;valid_=true;backend_.values(d.as<JsonObject>());d["sampled_at_ms"]=now;}
  core_.emit(path_,error?"failed":"completed",d.as<JsonObjectConst>(),owner_,now);if(!error)core_.emit(path_,"sample",d.as<JsonObjectConst>(),0,now);owner_=0;
 }
 void cancelOwner(uint32_t owner){if(pending_&&owner_==owner){error_=backend_.cancel();cleanup_=error_!=nullptr;finish("owner_released",core_.now());}}
public:
 static constexpr const char*Methods="describe,read,configure,measure,cancel";
 Sampling(Core&c,SampleBackend&backend,const char*path):core_(c),backend_(backend),path_(path){hook_=c.onOwnerRelease(this,[](void*p,uint32_t owner){static_cast<Sampling*>(p)->cancelOwner(owner);});}
 ~Sampling(){core_.removeOwnerHook(this);if(pending_||cleanup_)backend_.cancel();}
 void describe(JsonObject out)const{backend_.describe(out);out["configuration_scope"]="device";out["application_boundary"]="idle";out["read_semantics"]="last_valid_sample_no_bus_access";out["periodic_measurement"]="runtime_timers_or_rules";asyncContract(out,"measure",path_,{"completed","failed"})["operation_id"]="operation_id";}
 void poll(uint64_t now){
  if(cleanup_){if(!backend_.cancel())cleanup_=false;return;}if(!pending_)return;
  if(now>=deadline_){cleanup_=backend_.cancel()!=nullptr;finish("measurement_timeout",now);return;}
  auto result=backend_.poll(now);if(result.error){cleanup_=backend_.cancel()!=nullptr;finish(result.error,now);}else if(result.complete)finish(nullptr,now);
 }
 const char*invoke(const char*method,JsonObjectConst a,JsonObject out){
  if(!strcmp(method,"describe")){describe(out);return nullptr;}
  if(!strcmp(method,"read")){out["valid"]=valid_;out["pending"]=pending_;out["cleanup_pending"]=cleanup_;out["last_error"]=error_?error_:"";backend_.configuration(out["configuration"].to<JsonObject>());if(valid_){backend_.values(out);out["sampled_at_ms"]=sampled_;out["_meta"]["sampled_at_ms"]=sampled_;}return nullptr;}
  if(!strcmp(method,"configure")){if(pending_||cleanup_)return "sensor_busy";if(auto e=backend_.configure(a,core_.now()))return e;valid_=false;backend_.configuration(out["configuration"].to<JsonObject>());return nullptr;}
  if(!strcmp(method,"cancel")){
   for(auto f:a)if(strcmp(f.key().c_str(),"operation_id"))return "unknown_sample_argument";
   if(pending_&&core_.callerOwner()&&core_.callerOwner()!=owner_)return "operation_owned_by_other";
   if(a.containsKey("operation_id")&&(!a["operation_id"].is<uint32_t>()||a["operation_id"].as<uint32_t>()!=id_))return "stale_operation";
   if(pending_){cleanup_=backend_.cancel()!=nullptr;finish("cancelled",core_.now());}return nullptr;
  }
  if(strcmp(method,"measure"))return "unsupported_method";
  if(a.size())return "unknown_sample_argument";if(!hook_)return "owner_hook_capacity";if(pending_||cleanup_)return "sensor_busy";
  if(auto e=backend_.begin(core_.now()))return e;owner_=core_.callerOwner();id_=next_++;pending_=true;deadline_=core_.now()+backend_.timeoutMs();out["operation_id"]=id_;out["state"]="accepted";return nullptr;
 }
};
}
