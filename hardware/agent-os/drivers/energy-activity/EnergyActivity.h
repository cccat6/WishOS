#pragma once
#include "audio/Speech.h"
namespace notesos::audio {
// Amplitude activity detector, explicitly not a word recognizer. A neural VAD,
// WakeNet or another keyword adapter uses the same capture stream and contract.
class EnergyActivity final:public SpeechBackend {
 unsigned rate_,attack_=0,release_=0,above_=0,below_=0;float threshold_=0;bool active_=false;
public:
 explicit EnergyActivity(unsigned rate=16000):rate_(rate){}
 const char*id()const override{return "energy-activity.v1";}unsigned rate()const override{return rate_;}
 void describe(JsonObject o)const override{o["kind"]="audio_activity";o["sample_rate_hz"]=rate_;o["channels"]=1;o["parameters"]["threshold_dbfs"]="-96..0";o["parameters"]["attack_ms"]="1..5000";o["parameters"]["release_ms"]="1..10000";o["events"]="activity_started,activity_ended";}
 const char*validate(JsonObjectConst a)const override{
  for(auto p:a)if(strcmp(p.key().c_str(),"threshold_dbfs")&&strcmp(p.key().c_str(),"attack_ms")&&strcmp(p.key().c_str(),"release_ms"))return "unknown_detection_parameter";
  if(a.containsKey("threshold_dbfs")&&(!a["threshold_dbfs"].is<double>()||!std::isfinite(a["threshold_dbfs"].as<double>())||a["threshold_dbfs"].as<double>()< -96||a["threshold_dbfs"].as<double>()>0))return "activity_threshold";
  for(auto key:{"attack_ms","release_ms"})if(a.containsKey(key)&&(!a[key].is<unsigned>()||!a[key].as<unsigned>()||a[key].as<unsigned>()>(!strcmp(key,"attack_ms")?5000u:10000u)))return "activity_duration";
  return nullptr;
 }
 const char*start(JsonObjectConst a,ResourceBudget&)override{if(auto e=validate(a))return e;threshold_=std::pow(10.0,(a["threshold_dbfs"]|-35.0)/20.0)*32768;attack_=uint64_t(rate_)*(a["attack_ms"]|60u)/1000;release_=uint64_t(rate_)*(a["release_ms"]|300u)/1000;above_=below_=0;active_=false;return nullptr;}
 void process(const CaptureFrame&frame,Emit emit,void*context)override{const int16_t*p=frame.data;size_t n=frame.frames;if(!n)return;double sum=0;for(size_t i=0;i<n;++i)sum+=double(p[i])*p[i];float rms=std::sqrt(sum/n);
  if(rms>=threshold_){below_=0;above_=std::min<uint64_t>(uint64_t(above_)+n,attack_);if(!active_&&above_>=attack_){active_=true;emit(context,{"activity_started",nullptr,rms/32768});}}
  else{above_=0;below_=std::min<uint64_t>(uint64_t(below_)+n,release_);if(active_&&below_>=release_){active_=false;emit(context,{"activity_ended",nullptr,rms/32768});}}
 }

 void stop()override{active_=false;above_=below_=0;}
};
}
