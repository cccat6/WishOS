#pragma once
#include "audio/Speech.h"
#include <NotesWakeModels.h>
#include <esp_heap_caps.h>
namespace notesos::audio {
// Uses a pinned, compiled ESP-SR model catalog. Applications select a published
// engine and threshold; replacing model binaries is part of native composition.
class EspWakeNet final:public SpeechBackend {
 const char*id_;const char*model_;const char*words_;size_t workspace_;
 const esp_wn_iface_t*api_=nullptr;model_iface_data_t*state_=nullptr;
 ResourceBudget*memory_=nullptr;uint32_t reservation_=0;int16_t*block_=nullptr;size_t count_=0,at_=0;
public:
 EspWakeNet(const char*id,const char*model,const char*words,size_t workspace):id_(id),model_(model),words_(words),workspace_(workspace){}
 const char*id()const override{return id_;}unsigned rate()const override{return 16000;}
 void describe(JsonObject o)const override{o["kind"]="wake_word";o["backend"]="esp-sr-2.4.2-wakenet9s";o["model"]=model_;o["words"]=words_;o["sample_rate_hz"]=16000;o["channels"]=1;o["workspace_admission_bytes"]=workspace_;o["parameters"]["threshold"]="0.4..0.9999";o["phrase_selection"]="compiled_model";o["events"]="wake_word";}
 const char*validate(JsonObjectConst a)const override{for(auto p:a)if(strcmp(p.key().c_str(),"threshold"))return "unknown_detection_parameter";if(a.containsKey("threshold")&&(!a["threshold"].is<double>()||!std::isfinite(a["threshold"].as<double>())||a["threshold"].as<double>()<0.4||a["threshold"].as<double>()>0.9999))return "wake_threshold";return nullptr;}
 const char*start(JsonObjectConst a,ResourceBudget&m)override{
  if(auto e=validate(a))return e;api_=esp_wn_handle_from_name(model_);if(!api_)return "wake_model_not_available";
  memory_=&m;reservation_=m.acquire(workspace_,0,ResourceBudget::Region::Internal);if(!reservation_)return "speech_memory_budget";
  size_t before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);state_=api_->create(model_,DET_MODE_90);size_t after=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);m.used(reservation_,before>after?before-after:0);
  if(!state_){stop();return "wake_model_start_failed";}
  count_=api_->get_samp_chunksize(state_);if(api_->get_samp_rate(state_)!=16000||api_->get_channel_num(state_)!=1||!count_){stop();return "wake_model_format";}
  if(a.containsKey("threshold"))for(int word=1;word<=api_->get_word_num(state_);++word)if(!api_->set_det_threshold(state_,a["threshold"].as<float>(),word)){stop();return "wake_threshold_rejected";}
  block_=static_cast<int16_t*>(m.allocate(count_*sizeof(int16_t),ResourceBudget::Region::Internal));if(!block_){stop();return "speech_memory_budget";}at_=0;return nullptr;
 }
 void process(const CaptureFrame&frame,Emit emit,void*context)override{const int16_t*p=frame.data;size_t n=frame.frames;while(n){size_t take=std::min(n,count_-at_);memcpy(block_+at_,p,take*sizeof(int16_t));at_+=take;p+=take;n-=take;if(at_==count_){int word=api_->detect(state_,block_);at_=0;if(word>0)emit(context,{"wake_word",api_->get_word_name(state_,word),NAN});}}}
 void stop()override{if(state_){api_->destroy(state_);state_=nullptr;}if(memory_){memory_->freeAllocation(block_);memory_->release(reservation_);}block_=nullptr;reservation_=0;count_=at_=0;memory_=nullptr;}
};
}
