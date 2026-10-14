#pragma once
#include "Core.h"
#include <atomic>
namespace notesos {
// A compiled driver may place a blocking SDK operation on its native worker.
// Only the event-loop owner starts/polls it. The worker touches its driver context
// and cancel flag; it never runs application code or dispatches resource calls.
class NativeOperation {
public:
 struct Executor {bool(*launch)(void(*)(void*),void*)=nullptr;size_t memoryBytes=0;};
 using Work=const char*(*)(void*,const std::atomic<bool>&);
 using Settled=void(*)(void*);
private:
 Core&core_;const char*path_;Executor executor_;void*context_;Work work_;Settled settled_;
 std::atomic<bool>running_{false},done_{false},cancel_{false};
 uint32_t id_=0,next_=1,owner_=0,reservation_=0;char method_[32]{},error_[80]{};bool registered_=false;uint32_t*sequence_=nullptr;
 static void run(void*p){auto&s=*static_cast<NativeOperation*>(p);auto e=s.work_(s.context_,s.cancel_);if(e)copy(s.error_,sizeof s.error_,e);else s.error_[0]=0;s.done_.store(true,std::memory_order_release);}
public:
 NativeOperation(Core&c,const char*path,Executor executor,void*context,Work work,Settled settled=nullptr,uint32_t*sequence=nullptr):core_(c),path_(path),executor_(executor),context_(context),work_(work),settled_(settled),sequence_(sequence){
  registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<NativeOperation*>(p);if(s.owner_==owner&&s.active())s.cancel_=true;});
 }
 ~NativeOperation(){core_.removeOwnerHook(this);} // Native composition outlives its worker.
 bool active()const{return running_.load(std::memory_order_acquire);}
 const std::atomic<bool>&cancellation()const{return cancel_;}
 const char*start(const char*method,JsonObject out){
  if(active())return "operation_active";if(!registered_)return "owner_hook_capacity";
  if(!executor_.launch)return "native_executor_unavailable";
  auto&next=sequence_?*sequence_:next_;
  if(!method||strlen(method)>=sizeof method_||!next)return "operation_identity_limit";
  auto reserved=core_.resources().acquire(executor_.memoryBytes,core_.callerOwner());if(!reserved)return "native_operation_memory_budget";
  bool previousCancel=cancel_.load();uint32_t previous=id_,previousOwner=owner_;char priorMethod[sizeof method_],priorError[sizeof error_];memcpy(priorMethod,method_,sizeof method_);memcpy(priorError,error_,sizeof error_);
  reservation_=reserved;id_=next++;owner_=core_.callerOwner();copy(method_,sizeof method_,method);error_[0]=0;cancel_=done_=false;running_=true;
  if(!executor_.launch(run,this)){running_=false;cancel_=previousCancel;core_.resources().release(reservation_);reservation_=0;id_=previous;owner_=previousOwner;memcpy(method_,priorMethod,sizeof method_);memcpy(error_,priorError,sizeof error_);return "native_task_allocation_failed";}
  core_.resources().used(reservation_,executor_.memoryBytes);out["operation_id"]=id_;out["state"]="running";return nullptr;
 }
 void poll(){if(!active()||!done_.load(std::memory_order_acquire))return;
  running_.store(false,std::memory_order_release);core_.resources().release(reservation_);reservation_=0;
  if(settled_)settled_(context_);
  JsonDocument data;data["operation_id"]=id_;data["method"]=method_;data["ok"]=!*error_;data["state"]=*error_?"failed":"completed";data["error"]=error_;
  if(path_)core_.emit(path_,*error_?"operation_failed":"operation_completed",data.as<JsonObjectConst>(),owner_);
 }
 const char*cancel(JsonObjectConst a){for(auto field:a)if(strcmp(field.key().c_str(),"operation_id"))return "unknown_operation_argument";if(!a["operation_id"].is<uint32_t>()||a["operation_id"].as<uint32_t>()!=id_)return "operation_not_found";if(core_.callerOwner()&&core_.callerOwner()!=owner_)return "operation_owned_by_other";if(active())cancel_=true;return nullptr;}
 void read(JsonObject out)const{out["operation_id"]=id_;out["method"]=method_;out["state"]=active()?"running":!id_?"idle":*error_?"failed":"completed";out["cancel_requested"]=cancel_.load();if(!active())out["error"]=error_;}
 static void describe(JsonObject out,const char*method,const char*source){asyncContract(out,method,source,{"operation_completed","operation_failed"})["operation_id"]="operation_id";out["execution"]="native_worker";out["cancellation"]="cooperative_protocol_boundary";}
};
}
