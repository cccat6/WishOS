#pragma once
#include "../Core.h"
#include "CaptureFrame.h"
#include <cmath>
#include <chrono>
namespace notesos::audio {
struct Detection {const char*event=nullptr;const char*word=nullptr;float score=NAN;};
// Compiled recognition engines provide only detection. Model/phrase availability,
// format and parameter validation belong to the engine, never to Lua or a page.
class SpeechBackend {
public:
 using Emit=void(*)(void*,const Detection&);
 virtual ~SpeechBackend()=default;
 virtual const char*id()const=0;
 virtual unsigned rate()const=0;
 virtual const char*inputRoles()const{return "M";}
 virtual void describe(JsonObject)const=0;
 virtual const char*validate(JsonObjectConst)const=0;
 virtual const char*start(JsonObjectConst,ResourceBudget&)=0;
 virtual void process(const CaptureFrame&,Emit,void*)=0;
 virtual void stop()=0;
};
class SpeechService {
 Core&core_;const char*path_;SpeechBackend**backends_;size_t count_;
 const char*(*acquire_)(unsigned,const char*);void(*release_)();SpeechBackend*active_=nullptr;
 uint32_t owner_=0,session_=0,next_=1;mutable std::mutex mutex_;
 uint64_t processingUs_=0,processingSamples_=0,peakUs_=0;bool hook_;
 void detected(const Detection&result){JsonDocument d;d["session_id"]=session_;d["engine"]=active_->id();if(result.word)d["word"]=result.word;if(std::isfinite(result.score))d["score"]=result.score;core_.emit(path_,result.event,d.as<JsonObjectConst>(),owner_);}
 void stopLocked(){if(!active_)return;active_->stop();active_=nullptr;release_();JsonDocument d;d["session_id"]=session_;core_.emit(path_,"stopped",d.as<JsonObjectConst>(),owner_);owner_=0;}
public:
 SpeechService(Core&c,const char*path,SpeechBackend**engines,size_t n,const char*(*acquire)(unsigned,const char*),void(*release)()):core_(c),path_(path),backends_(engines),count_(n),acquire_(acquire),release_(release){hook_=c.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<SpeechService*>(p);std::lock_guard<std::mutex>lock(s.mutex_);if(s.owner_==owner)s.stopLocked();});}
 ~SpeechService(){core_.removeOwnerHook(this);std::lock_guard<std::mutex>lock(mutex_);stopLocked();}
 bool active()const{std::lock_guard<std::mutex>lock(mutex_);return active_!=nullptr;}
 void describe(JsonObject o)const{auto list=o["engines"].to<JsonArray>();for(size_t i=0;i<count_;++i){auto e=list.add<JsonObject>();e["id"]=backends_[i]->id();backends_[i]->describe(e);e["input_roles"]=backends_[i]->inputRoles();}o["input"]="shared_microphone_pcm";o["settings_scope"]="session";o["application_boundary"]="start";o["actions"]="application_event_handlers";o["power_states"]="capture_peripheral_and_processing_core_running";o["events"]="started,stopped,failed; engine events declared per engine";}
 // Called only by the input worker. It never executes application code.
 void process(const CaptureFrame&frame){std::lock_guard<std::mutex>lock(mutex_);if(!active_)return;
  if(frame.rate!=active_->rate()||strcmp(frame.roles,active_->inputRoles())){JsonDocument d;d["session_id"]=session_;d["error"]="capture_format_changed";core_.emit(path_,"failed",d.as<JsonObjectConst>(),owner_);stopLocked();return;}auto before=std::chrono::steady_clock::now();active_->process(frame,[](void*p,const Detection&result){static_cast<SpeechService*>(p)->detected(result);},this);uint64_t elapsed=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-before).count();processingUs_+=elapsed;processingSamples_+=frame.frames;peakUs_=std::max(peakUs_,elapsed);}

 void inputFailed(const char*error){std::lock_guard<std::mutex>lock(mutex_);if(!active_)return;JsonDocument d;d["session_id"]=session_;d["error"]=error;core_.emit(path_,"failed",d.as<JsonObjectConst>(),owner_);stopLocked();}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){std::lock_guard<std::mutex>lock(mutex_);
  if(!strcmp(m,"read")){o["processing_us"]=processingUs_;o["processing_peak_us"]=peakUs_;o["processed_samples"]=processingSamples_;o["active"]=active_!=nullptr;o["session_id"]=session_;if(active_){o["engine"]=active_->id();o["owner"]=owner_;o["sample_rate_hz"]=active_->rate();}return nullptr;}
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(active_&&core_.callerOwner()&&owner_!=core_.callerOwner())return "speech_owned_by_other";
  if(!strcmp(m,"stop")){if(a.size())return "unknown_detection_argument";stopLocked();return nullptr;}
  if(strcmp(m,"start"))return "unsupported_method";if(!hook_)return "owner_hook_capacity";if(active_)return "speech_active";
  for(auto p:a)if(strcmp(p.key().c_str(),"engine")&&strcmp(p.key().c_str(),"parameters"))return "unknown_detection_argument";
  if(!a["engine"].is<const char*>()||(!a["parameters"].isNull()&&!a["parameters"].is<JsonObjectConst>()))return "speech_configuration";
  SpeechBackend*engine=nullptr;for(size_t i=0;i<count_;++i)if(!strcmp(a["engine"],backends_[i]->id()))engine=backends_[i];if(!engine)return "speech_engine_not_found";
  auto args=a["parameters"].as<JsonObjectConst>();if(auto e=engine->validate(args))return e;
  if(auto e=engine->start(args,core_.resources()))return e;if(auto e=acquire_(engine->rate(),engine->inputRoles())){engine->stop();return e;}
  processingUs_=processingSamples_=peakUs_=0;active_=engine;owner_=core_.callerOwner();session_=next_++;o["session_id"]=session_;o["state"]="started";core_.emit(path_,"started",o,owner_);return nullptr;
 }
};
}
