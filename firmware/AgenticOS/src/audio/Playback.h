#pragma once
#include "../Core.h"
#include "../WireBytes.h"
#include "../VoiceCodec.h"
#include <cmath>
namespace notesos::audio {
struct PcmFormat{unsigned rate=16000,channels=1;};
class OutputBackend {
public:
 virtual ~OutputBackend()=default;
 virtual void describe(JsonObject)const=0;
 virtual const char*validate(PcmFormat)const=0;
 virtual const char*open(PcmFormat)=0;
 // Bounded/nonblocking calls from the audio worker. accepted is interleaved
 // PCM16 samples. drained means hardware DMA completed, not acoustic measurement.
 virtual const char*write(const int16_t*,size_t,size_t&accepted)=0;
 virtual bool drained()=0;
 virtual void close()=0;
};
class Playback {
 static constexpr size_t PACKET_BYTES=1024,PCM_SAMPLES=1024;
 struct Packet{uint32_t sequence=0;uint16_t size=0;uint8_t bytes[PACKET_BYTES];};
 Core&core_;OutputBackend&backend_;const char*path_;mutable std::mutex mutex_;
 Packet*queue_=nullptr;int16_t*pcm_=nullptr;size_t capacity_=0,head_=0,count_=0,pcmCount_=0,pcmAt_=0;
 PcmFormat format_;uint32_t id_=0,next_=1,owner_=0,expected_=0;uint64_t played_=0;
 enum class State{Idle,Opening,Playing,Paused,Stopping,Done,Failed};State state_=State::Idle;
 bool adpcm_=false,finished_=false,needNotified_=false,backendOpen_=false,registered_=false;float volume_=1;bool mute_=false;const char*error_=nullptr;
 static const char*name(State s){switch(s){case State::Idle:return "idle";case State::Opening:return "opening";case State::Playing:return "playing";case State::Paused:return "paused";case State::Stopping:return "stopping";case State::Done:return "completed";default:return "failed";}}
 bool running()const{return state_==State::Opening||state_==State::Playing||state_==State::Paused||state_==State::Stopping;}
 void notify(const char*event){JsonDocument d;d["playback_id"]=id_;d["played_frames"]=played_/format_.channels;d["completion_basis"]="output_backend_drain";if(error_)d["error"]=error_;core_.emit(path_,event,d.as<JsonObjectConst>(),owner_);}
 void freeBuffers(){core_.resources().freeAllocation(queue_);core_.resources().freeAllocation(pcm_);queue_=nullptr;pcm_=nullptr;capacity_=count_=pcmCount_=pcmAt_=0;}
 void end(State result,const char*error=nullptr){if(backendOpen_){backend_.close();backendOpen_=false;}state_=result;error_=error;freeBuffers();notify(result==State::Done?"completed":"failed");}
public:
 Playback(Core&c,OutputBackend&backend,const char*path="/dev/speaker"):core_(c),backend_(backend),path_(path){registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<Playback*>(p);std::lock_guard<std::mutex>lock(s.mutex_);if(s.owner_==owner&&s.running())s.state_=State::Stopping;});}
 ~Playback(){core_.removeOwnerHook(this);if(backendOpen_)backend_.close();freeBuffers();}
 bool active()const{std::lock_guard<std::mutex>lock(mutex_);return running();}
 void describe(JsonObject o)const{backend_.describe(o);o["encodings"]="pcm16le,ima-adpcm-block";o["max_packet_bytes"]=PACKET_BYTES;o["max_decoded_samples"]=PCM_SAMPLES;o["buffer_policy"]="caller_blocks_admitted_from_resource_budget";o["settings_scope"]="playback";o["application_boundary"]="start";o["volume_scope"]="driver_runtime";o["volume_apply"]="next_pcm_block";o["generation"]="application_or_upstream_service";o["events"]="started,need_data,completed,failed";asyncContract(o,"start",path_)["playback_id"]="playback_id";}
 // The BSP schedules this on its output worker, not the application event loop.
 void pump(){std::lock_guard<std::mutex>lock(mutex_);if(!running())return;
  if(state_==State::Stopping){end(State::Failed,"cancelled");return;}
  if(state_==State::Opening){if(auto e=backend_.open(format_)){end(State::Failed,e);return;}backendOpen_=true;state_=State::Playing;notify("started");}
  if(state_==State::Paused)return;
  if(pcmAt_==pcmCount_){pcmAt_=pcmCount_=0;
   if(!count_){if(finished_){if(backend_.drained())end(State::Done);}else if(!needNotified_){needNotified_=true;notify("need_data");}return;}
   auto&p=queue_[head_];if(adpcm_){if(!decodeVoiceBlock(p.bytes,p.size,pcm_,PCM_SAMPLES,pcmCount_)){end(State::Failed,"audio_decode_failed");return;}}
   else{pcmCount_=p.size/2;for(size_t i=0;i<pcmCount_;++i)pcm_[i]=int16_t(uint16_t(p.bytes[i*2])|(uint16_t(p.bytes[i*2+1])<<8));}
   for(size_t i=0;i<pcmCount_;++i)pcm_[i]=mute_?0:int16_t(std::lround(pcm_[i]*volume_));
   head_=(head_+1)%capacity_;--count_;needNotified_=false;
  }
  size_t accepted=0;if(auto e=backend_.write(pcm_+pcmAt_,pcmCount_-pcmAt_,accepted)){end(State::Failed,e);return;}pcmAt_+=accepted;played_+=accepted;
 }
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){std::lock_guard<std::mutex>lock(mutex_);
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(!strcmp(m,"read")){o["state"]=name(state_);o["playback_id"]=id_;o["owner"]=owner_;o["queued_blocks"]=count_;o["buffer_blocks"]=capacity_;o["played_frames"]=played_/format_.channels;o["input_finished"]=finished_;o["volume"]=volume_;o["mute"]=mute_;o["error"]=error_;return nullptr;}
  if(running()&&core_.callerOwner()&&core_.callerOwner()!=owner_)return "playback_owned_by_other";
  if(!strcmp(m,"volume")){for(auto p:a)if(strcmp(p.key().c_str(),"gain")&&strcmp(p.key().c_str(),"mute"))return "unknown_volume_argument";
   if(a.containsKey("gain")&&(!a["gain"].is<double>()||!std::isfinite(a["gain"].as<double>())||a["gain"].as<double>()<0||a["gain"].as<double>()>1))return "volume_range";
   if(a.containsKey("mute")&&!a["mute"].is<bool>())return "mute_type";volume_=a["gain"]|volume_;mute_=a["mute"]|mute_;return nullptr;}
  if(!strcmp(m,"start")){
   if(!registered_)return "owner_hook_capacity";if(running())return "playback_active";
   for(auto p:a)if(strcmp(p.key().c_str(),"sample_rate_hz")&&strcmp(p.key().c_str(),"channels")&&strcmp(p.key().c_str(),"encoding")&&strcmp(p.key().c_str(),"buffer_blocks"))return "unknown_playback_argument";
   for(auto key:{"sample_rate_hz","channels","buffer_blocks"})if(a.containsKey(key)&&!a[key].is<unsigned>())return "audio_integer_required";
   if(a.containsKey("encoding")&&!a["encoding"].is<const char*>())return "audio_encoding_required";
   PcmFormat f{a["sample_rate_hz"]|16000u,a["channels"]|1u};const char*encoding=a["encoding"]|"pcm16le";bool compressed=!strcmp(encoding,"ima-adpcm-block");
   if((strcmp(encoding,"pcm16le")&&!compressed)||(compressed&&f.channels!=1))return "audio_encoding_not_supported";
   if(auto e=backend_.validate(f))return e;unsigned blocks=a["buffer_blocks"]|4u;if(!blocks||blocks>SIZE_MAX/sizeof(Packet))return "audio_buffer_range";
   auto q=static_cast<Packet*>(core_.resources().allocate(size_t(blocks)*sizeof(Packet)));if(!q)return "audio_memory_budget";auto pcm=static_cast<int16_t*>(core_.resources().allocate(PCM_SAMPLES*sizeof(int16_t)));
   if(!pcm){core_.resources().freeAllocation(q);return "audio_memory_budget";}
   queue_=q;pcm_=pcm;capacity_=blocks;head_=count_=pcmCount_=pcmAt_=0;played_=0;expected_=0;finished_=needNotified_=false;format_=f;adpcm_=compressed;owner_=core_.callerOwner();id_=next_++;error_=nullptr;state_=State::Opening;o["playback_id"]=id_;o["state"]="opening";return nullptr;
  }
  if(!a["playback_id"].is<uint32_t>()||a["playback_id"].as<uint32_t>()!=id_)return "playback_not_found";
  if(!running()||state_==State::Stopping)return "playback_not_active";
  for(auto p:a)if(strcmp(p.key().c_str(),"playback_id")&&(!strcmp(m,"write")?(strcmp(p.key().c_str(),"sequence")&&strcmp(p.key().c_str(),"hex")):true))return "unknown_playback_argument";
  if(!strcmp(m,"stop")){state_=State::Stopping;return nullptr;}
  if(!strcmp(m,"pause")){if(state_!=State::Playing)return "playback_not_playing";state_=State::Paused;return nullptr;}
  if(!strcmp(m,"resume")){if(state_!=State::Paused)return "playback_not_paused";state_=State::Playing;return nullptr;}
  if(!strcmp(m,"finish")){finished_=true;return nullptr;}
  if(strcmp(m,"write"))return "unsupported_method";if(finished_)return "playback_input_closed";
  if(!a["sequence"].is<uint32_t>()||!a["hex"].is<const char*>())return "audio_packet_required";
  auto sequence=a["sequence"].as<uint32_t>();if(sequence<expected_){o["duplicate"]=true;return nullptr;}if(sequence!=expected_)return "audio_sequence_gap";if(count_==capacity_)return "audio_would_block";
  auto&p=queue_[(head_+count_)%capacity_];size_t bytes=0;if(!unhex(a["hex"],p.bytes,sizeof p.bytes,bytes)||!bytes)return "audio_packet_size";
  if(!adpcm_&&bytes%(2*format_.channels))return "audio_frame_alignment";
  if(adpcm_&&(bytes<11||(!p.bytes[9]&&!p.bytes[10])||p.bytes[0]!=0x42||p.bytes[7]>88||p.bytes[8]||size_t(p.bytes[9]+(unsigned(p.bytes[10])<<8))>PCM_SAMPLES||bytes!=11+(p.bytes[9]+(unsigned(p.bytes[10])<<8)+1)/2))return "audio_packet_format";
  p.size=bytes;p.sequence=sequence;++count_;++expected_;needNotified_=false;o["accepted_bytes"]=bytes;o["next_sequence"]=expected_;return nullptr;
 }
};
}
