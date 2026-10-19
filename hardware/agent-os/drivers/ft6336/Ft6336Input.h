#pragma once
#include "Ft6336.h"
#include "NativeOperation.h"
#include "InputPresentation.h"
#include <atomic>
#include <mutex>
namespace notesos::drivers {
// Optional native input stream. Capture runs independently of UI computation;
// only the OS loop publishes events/calls applications. This is a driver, not
// an OS gesture policy. The backend is private to this native acquisition task.
class Ft6336Input {
 struct Sample{Ft6336::Frame frame;uint32_t owner=0;InputPresentation presentation;uint32_t epoch=0;uint64_t at=0;};
 Core&core_;Ft6336&backend_;InputPresentation(*presentation_)();uint32_t(*legacyPresentation_)()=nullptr;void(*delay_)(unsigned);
 InputPresentation capturePresentation()const{return presentation_?presentation_():InputPresentation{legacyPresentation_?legacyPresentation_():0,0,0};}
 std::atomic<bool>enabled_{false};std::atomic<uint32_t>owner_{0},period_{20},epoch_{0},dropped_{0};std::atomic<const char*>error_{nullptr};
 mutable std::mutex mutex_;Sample*queue_=nullptr,last_{};size_t head_=0,count_=0;bool valid_=false,hook_=false;uint64_t startDeadline_=0;uint32_t observedDrops_=0;const char*observedError_=nullptr;
 uint32_t sampleErrors_=0,consecutiveErrors_=0;
 static constexpr unsigned Capacity=32,FailureLimit=3,FailureRetryMs=100;
 NativeOperation operation_;
 static bool same(const Ft6336::Frame&a,const Ft6336::Frame&b){if(a.count!=b.count)return false;for(unsigned i=0;i<a.count;++i)if(a.contacts[i].x!=b.contacts[i].x||a.contacts[i].y!=b.contacts[i].y||a.contacts[i].id!=b.contacts[i].id||a.contacts[i].event!=b.contacts[i].event||a.contacts[i].weight!=b.contacts[i].weight||a.contacts[i].area!=b.contacts[i].area)return false;return true;}
 static const char*run(void*p,const std::atomic<bool>&cancel){auto&s=*static_cast<Ft6336Input*>(p);bool previous=false;Sample last;uint32_t lastEpoch=0;
  while(!cancel){uint32_t owner,epoch;{std::lock_guard<std::mutex>lock(s.mutex_);if(!s.enabled_)break;owner=s.owner_;epoch=s.epoch_;}
   auto presentation=s.capturePresentation();auto error=s.backend_.begin(s.core_.now());bool complete=false;auto deadline=s.core_.now()+s.backend_.timeoutMs();
   while(!error&&!complete&&s.enabled_&&epoch==s.epoch_&&s.core_.now()<deadline){auto result=s.backend_.poll(s.core_.now());error=result.error;complete=result.complete;if(!error&&!complete)s.delay_(1);}
   if(!complete&&!error&&s.enabled_&&epoch==s.epoch_)error="touch_input_timeout";
   if(!complete)s.backend_.cancel();
   // Owner/configuration replacement and the sample's final publication are
   // one state transaction. Physical I/O and its waits stay outside this lock.
   {std::lock_guard<std::mutex>lock(s.mutex_);if(epoch!=s.epoch_||owner!=s.owner_||!s.enabled_)continue;
    if(error){++s.sampleErrors_;if(++s.consecutiveErrors_>=FailureLimit){s.error_=error;s.enabled_=false;previous=false;}}else if(complete){s.error_=nullptr;s.consecutiveErrors_=0;Sample next{s.backend_.frame(),owner,presentation,epoch,s.core_.now()};
     if(!previous||epoch!=lastEpoch||next.presentation!=last.presentation||!same(next.frame,last.frame)){if(s.count_==Capacity){s.head_=s.count_=0;++s.dropped_;}s.queue_[(s.head_+s.count_)%Capacity]=next;++s.count_;s.last_=next;s.valid_=true;last=next;lastEpoch=epoch;previous=true;}}
   }
   auto interval=s.period_.load();s.delay_(error&&interval<FailureRetryMs?FailureRetryMs:interval);
  }s.backend_.cancel();return nullptr;
 }
 const char*launch(){bool created=!queue_;if(created){queue_=static_cast<Sample*>(core_.resources().allocate(Capacity*sizeof(Sample)));if(!queue_)return "touch_input_memory_budget";head_=count_=0;}
  JsonDocument out;auto e=operation_.start("input",out.to<JsonObject>(),0);if(e&&created){std::lock_guard<std::mutex>lock(mutex_);core_.resources().freeAllocation(queue_);queue_=nullptr;head_=count_=0;}return e;
 }
 void failure(const char*error){JsonDocument data;data["error"]=error;data["input_stream"]=true;core_.emit("/dev/touch","failed",data.as<JsonObjectConst>(),owner_);}
public:
 Ft6336Input(Core&core,Ft6336&backend,NativeOperation::Executor executor,InputPresentation(*presentation)(),void(*delay)(unsigned)):core_(core),backend_(backend),presentation_(presentation),delay_(delay),operation_(core,nullptr,executor,this,run ){hook_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<Ft6336Input*>(p);std::lock_guard<std::mutex>lock(s.mutex_);if(s.owner_==owner){s.enabled_=false;++s.epoch_;}});}
 Ft6336Input(Core&core,Ft6336&backend,NativeOperation::Executor executor,uint32_t(*presentation)(),void(*delay)(unsigned)):Ft6336Input(core,backend,executor,static_cast<InputPresentation(*)()>(nullptr),delay){legacyPresentation_=presentation;}
 ~Ft6336Input(){core_.removeOwnerHook(this);core_.resources().freeAllocation(queue_);} // Native composition outlives the task.
 void describe(JsonObject o)const{auto s=o["input_stream"].to<JsonObject>();s["method"]="configure_stream";s["event"]="input";s["interval_ms"]=20;s["minimum_interval_ms"]=5;s["maximum_interval_ms"]=1000;s["queue_frames"]=Capacity;s["terminal_failure_after_consecutive_errors"]=FailureLimit;s["error_retry_interval_ms"]=FailureRetryMs;s["timestamp"]="physical_capture";s["presentation_id"]="capture_time_device_context";s["presentation_revision"]="capture_time_device_context";s["presentation_activation"]="capture_time_device_context";s["lifetime"]="calling_instance_or_system";s["configuration_boundary"]="next_native_sample";}
 void read(JsonObject o)const{Sample last;bool valid,enabled;uint32_t owner,period,dropped,sampleErrors,consecutiveErrors;const char*error;{std::lock_guard<std::mutex>lock(mutex_);valid=valid_;last=last_;enabled=enabled_;owner=owner_;period=period_;dropped=dropped_;sampleErrors=sampleErrors_;consecutiveErrors=consecutiveErrors_;error=error_;}
  auto s=o["input_stream"].to<JsonObject>();s["enabled"]=enabled;s["running"]=operation_.active();s["owner"]=owner;s["interval_ms"]=period;s["dropped_batches"]=dropped;s["sample_errors"]=sampleErrors;s["consecutive_errors"]=consecutiveErrors;s["error"]=error?error:"";s["valid"]=valid;if(valid){s["sampled_at_ms"]=last.at;s["presentation_id"]=last.presentation.view;s["presentation_revision"]=last.presentation.revision;s["presentation_activation"]=last.presentation.activation;Ft6336::values(last.frame,s["sample"].to<JsonObject>());}}
 const char*configure(JsonObjectConst a,JsonObject o){if(auto e=argumentFields(a,"enabled,interval_ms"))return e;if(a["enabled"]==true&&!hook_)return "owner_hook_capacity";if(!a["enabled"].is<bool>()||a.containsKey("interval_ms")&&!a["interval_ms"].is<uint32_t>())return "touch_input_setting_type";auto caller=core_.callerOwner();
  bool changed=false,wasEnabled=false;uint32_t previousOwner=0,previousPeriod=0,previousEpoch=0,previousConsecutiveErrors=0;uint64_t previousDeadline=0;const char*previousError=nullptr;
  {std::lock_guard<std::mutex>lock(mutex_);auto period=a["interval_ms"]|period_.load();if(period<5||period>1000)return "touch_input_interval";
   if(enabled_&&caller&&owner_!=caller){if(!(owner_==0&&a["enabled"]==true&&period==period_))return "touch_input_owned_by_other";}
   else if(a["enabled"]==false){if(enabled_){enabled_=false;++epoch_;consecutiveErrors_=0;}}
   else if(!(enabled_&&owner_==caller&&period==period_)){wasEnabled=enabled_;previousOwner=owner_;previousPeriod=period_;previousEpoch=epoch_;previousConsecutiveErrors=consecutiveErrors_;previousDeadline=startDeadline_;previousError=error_;owner_=caller;period_=period;enabled_=true;error_=nullptr;consecutiveErrors_=0;++epoch_;startDeadline_=core_.now()+3000;changed=true;}
  }
  if(changed&&!operation_.active())if(auto e=launch()){std::lock_guard<std::mutex>lock(mutex_);enabled_=wasEnabled;owner_=previousOwner;period_=previousPeriod;epoch_=previousEpoch;consecutiveErrors_=previousConsecutiveErrors;startDeadline_=previousDeadline;error_=previousError;return e;}
  if(changed)observedError_=nullptr;read(o);return nullptr;
 }
 void poll(){operation_.poll();if(!operation_.active()){if(enabled_){if(auto e=launch()){if(core_.now()>=startDeadline_){{std::lock_guard<std::mutex>lock(mutex_);enabled_=false;++epoch_;}failure(e);}}}else{std::lock_guard<std::mutex>lock(mutex_);core_.resources().freeAllocation(queue_);queue_=nullptr;head_=count_=0;}}
  auto error=error_.load();if(error!=observedError_){observedError_=error;if(error)failure(error);}auto drops=dropped_.load();if(drops!=observedDrops_){observedDrops_=drops;JsonDocument data;data["input_stream"]=true;core_.emit("/dev/touch","input_overflow",data.as<JsonObjectConst>(),owner_);}
  for(unsigned i=0;i<8;++i){Sample sample;{std::lock_guard<std::mutex>lock(mutex_);if(!count_)break;sample=queue_[head_];head_=(head_+1)%Capacity;--count_;}if(!enabled_||sample.owner!=owner_||sample.epoch!=epoch_)continue;JsonDocument data;Ft6336::values(sample.frame,data.to<JsonObject>());data["sampled_at_ms"]=sample.at;data["presentation_id"]=sample.presentation.view;data["presentation_revision"]=sample.presentation.revision;data["presentation_activation"]=sample.presentation.activation;core_.emit("/dev/touch","input",data.as<JsonObjectConst>(),sample.owner,sample.at);}
 }
};
}
