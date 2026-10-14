#pragma once
#include "../Core.h"
#include "../Policy.h"
namespace notesos::actuation {
struct Limits {uint32_t defaultLeaseMs,maxLeaseMs;bool allowUnbounded,retarget;};
struct Observation {enum class State{Running,Complete,Failed}state=State::Running;const char*basis="driver_feedback";const char*error=nullptr;};
class Backend {
public:
 virtual ~Backend()=default;
 virtual Limits limits()const=0;
 virtual void describe(JsonObject)const=0;
 virtual const char*validate(JsonObjectConst command)const=0;
 virtual const char*apply(JsonObjectConst command)=0;
 virtual const char*stop()=0;
 virtual Observation poll()=0;
 virtual void read(JsonObject)const=0;
};
// Optional compiled service, not an OS-wide actuator taxonomy. The adapter owns
// electrical sequencing, units, interlocks and feedback. Apps own motion plans.
class Actuator {
 Core&core_;Backend&backend_;const char*path_;mutable std::mutex mutex_;
 uint32_t owner_=0,lease_=0,nextLease_=1,command_=0,nextCommand_=1;uint64_t deadline_=0;
 bool pending_=false,fault_=false,registered_=false;const char*lastError_=nullptr;
 const char*duration(JsonObjectConst a,uint32_t&ms)const{auto l=backend_.limits();if(a.containsKey("lease_ms")&&!a["lease_ms"].is<uint32_t>())return "lease_duration_type";ms=a["lease_ms"]|l.defaultLeaseMs;if((!ms&&!l.allowUnbounded)||(l.maxLeaseMs&&ms>l.maxLeaseMs))return "lease_duration_range";return nullptr;}
 void event(const char*name,const char*basis,const char*error=nullptr){JsonDocument d;d["lease_id"]=lease_;d["command_id"]=command_;d["completion"]["basis"]=basis;if(error)d["error"]=error;core_.emit(path_,name,d.as<JsonObjectConst>(),owner_);}
 const char*halt(const char*reason,bool release){auto error=backend_.stop();lastError_=error;fault_=error!=nullptr;if(pending_){event(error?"failed":"cancelled","driver_stop_issued",error?error:reason);pending_=false;}if(error)return error;if(release){event("released","control_lease_released",reason);lease_=0;owner_=0;deadline_=0;}return nullptr;}
public:
 Actuator(Core&c,Backend&b,const char*path):core_(c),backend_(b),path_(path){registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<Actuator*>(p);std::lock_guard<std::mutex>lock(s.mutex_);if(s.lease_&&s.owner_==owner)s.halt("owner_exit",true);});}
 ~Actuator(){core_.removeOwnerHook(this);if(lease_)backend_.stop();}
 void describe(JsonObject o)const{backend_.describe(o);auto l=backend_.limits();o["control"]="exclusive_lease";o["default_lease_ms"]=l.defaultLeaseMs;o["max_lease_ms"]=l.maxLeaseMs;o["unbounded_lease_supported"]=l.allowUnbounded;o["retarget_supported"]=l.retarget;o["settings_scope"]="control_lease";o["electrical_control"]="compiled_adapter";o["events"]="completed,failed,cancelled,superseded,released";asyncContract(o,"command",path_,{"completed","failed","cancelled","superseded"})["command_id"]="command_id";}
 // Schedule from the native adapter's control task. Script/network progress is
 // never the clock that enforces an actuator's control-lease expiry.
 void poll(uint64_t now){std::lock_guard<std::mutex>lock(mutex_);if(!lease_)return;if(deadline_&&now>=deadline_){halt("lease_expired",true);return;}if(!pending_)return;auto result=backend_.poll();if(result.state==Observation::State::Running)return;
  lastError_=result.error;fault_=result.state==Observation::State::Failed;pending_=false;event(fault_?"failed":"completed",result.basis,result.error);if(fault_)backend_.stop();
 }
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o,uint64_t now){std::lock_guard<std::mutex>lock(mutex_);
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(!strcmp(m,"read")){o["lease_id"]=lease_;o["owner"]=owner_;o["command_id"]=command_;o["command_pending"]=pending_;o["fault"]=fault_;o["error"]=lastError_;if(deadline_)o["lease_deadline_ms"]=deadline_;else o["lease_deadline_ms"]=nullptr;backend_.read(o["feedback"].to<JsonObject>());return nullptr;}
  const char*allowed=!strcmp(m,"acquire")?"lease_ms":!strcmp(m,"renew")?"lease_id,lease_ms":!strcmp(m,"command")?"lease_id,command,replace":"lease_id";
  for(auto f:a)if(!Policies::choice(allowed,f.key().c_str()))return "unknown_actuator_argument";
  if(!strcmp(m,"acquire")){if(!registered_)return "owner_hook_capacity";if(lease_)return "actuator_owned";if(fault_)return "actuator_fault";uint32_t ms;if(auto e=duration(a,ms))return e;lease_=nextLease_++;owner_=core_.callerOwner();deadline_=ms?now+ms:0;o["lease_id"]=lease_;return nullptr;}
  if(!lease_||!a["lease_id"].is<uint32_t>()||a["lease_id"].as<uint32_t>()!=lease_)return "actuator_lease_not_found";
  if(core_.callerOwner()&&core_.callerOwner()!=owner_)return "actuator_owned_by_other";
  if(deadline_&&now>=deadline_){halt("lease_expired",true);return "actuator_lease_expired";}
  if(!strcmp(m,"stop"))return halt("stop_requested",false);
  if(!strcmp(m,"release"))return halt("release_requested",true);
  if(!strcmp(m,"renew")){uint32_t ms;if(auto e=duration(a,ms))return e;deadline_=ms?now+ms:0;o["lease_id"]=lease_;return nullptr;}
  if(strcmp(m,"command"))return "unsupported_method";if(fault_)return "actuator_fault";
  if(!a["command"].is<JsonObjectConst>()||(a.containsKey("replace")&&!a["replace"].is<bool>()))return "actuator_command_type";
  if(auto e=backend_.validate(a["command"].as<JsonObjectConst>()))return e;
  if(pending_&&(!(a["replace"]|false)||!backend_.limits().retarget))return "actuator_command_active";
  if(pending_)event("superseded","retarget_accepted");
  command_=nextCommand_++;pending_=true;o["command_id"]=command_;o["lease_id"]=lease_;o["state"]="accepted";
  if(auto e=backend_.apply(a["command"].as<JsonObjectConst>())){pending_=false;fault_=true;lastError_=e;backend_.stop();event("failed","driver_apply_failed",e);}
  return nullptr;
 }
};
}
