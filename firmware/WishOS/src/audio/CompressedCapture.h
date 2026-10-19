#pragma once
#include "RecordingBuffer.h"
#include "CaptureFrame.h"
#include "RecordingStorage.h"
#include "../VoiceCodec.h"
namespace notesos::audio {
// Transport-independent compressed recording. Native capture owns PCM/clock
// acquisition; this service owns recording identities, bounds, loss and receipts.
class CompressedCapture {
public:
 static constexpr const char*ReadMethods="read,describe,list";
 using Begin=const char*(*)(unsigned);using Release=void(*)();
 using Sink=void(*)(const char*,const RecordingBuffer<>::Chunk&);
private:
 Core&core_;RecordingBuffer<>buffer_;const char*boot_;const char*path_;const char*roles_;Begin begin_;Release release_;Sink sink_=nullptr;CaptureStorage*storage_=nullptr;mutable std::mutex mutex_;
 bool recording_=false,stop_=false,started_=false;unsigned rate_=16000,channel_=0;uint32_t owner_=0,next_=1,sequence_=0,maxMs_=0;uint64_t samples_=0;char id_[40]{};VoiceCodec codec_;
 void finishLocked(const char*error){recording_=false;buffer_.finish(error,core_.now());if(storage_)storage_->finish(id_,error);release_();JsonDocument d;d["recording_id"]=id_;if(error)d["error"]=error;core_.emit(path_,error?"failed":"stopped",d.as<JsonObjectConst>(),owner_);}
public:
 CompressedCapture(Core&c,const char*boot,Begin begin,Release release,const char*path="/dev/mic",const char*roles="M"):core_(c),buffer_(c.resources()),boot_(boot),path_(path),roles_(roles),begin_(begin),release_(release){c.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<CompressedCapture*>(p);std::lock_guard<std::mutex>lock(s.mutex_);if(s.recording_&&s.owner_==owner)s.stop_=true;s.buffer_.discardOwner(owner);});}
 ~CompressedCapture(){core_.removeOwnerHook(this);}
 void storage(CaptureStorage&storage){storage_=&storage;}
 void storageStatus(const char*id,const char*file,bool pending,bool durable,const char*error,uint64_t missing){buffer_.storage(id,file,pending,durable,error,missing);}
 void poll(){if(storage_)storage_->poll();std::lock_guard<std::mutex>lock(mutex_);if(recording_&&stop_&&!started_)finishLocked(nullptr);buffer_.reap(core_.now());}
 bool active()const{std::lock_guard<std::mutex>lock(mutex_);return recording_;}
 void sink(Sink sink){sink_=sink;}
 const char*id()const{return id_;}
 void failed(const char*e){std::lock_guard<std::mutex>lock(mutex_);if(recording_)finishLocked(e);}
 void process(const CaptureFrame&frame){std::lock_guard<std::mutex>lock(mutex_);if(!recording_)return;if(stop_||(maxMs_&&samples_*1000>=uint64_t(maxMs_)*rate_)){finishLocked(nullptr);return;}size_t channels=strlen(frame.roles);if(channel_>=channels||frame.rate!=rate_){finishLocked("capture_format_changed");return;}
  size_t at=0;while(at<frame.frames){
   uint64_t limit=maxMs_?uint64_t(maxMs_)*rate_/1000:UINT64_MAX;
   if(samples_>=limit){finishLocked(nullptr);return;}
   size_t frames=std::min<uint64_t>(std::min<size_t>(frame.frames-at,160),limit-samples_);if(!frames)return;
   if(!started_){started_=true;JsonDocument d;d["recording_id"]=id_;core_.emit(path_,"started",d.as<JsonObjectConst>(),owner_);}
   RecordingBuffer<>::Chunk chunk{};chunk.sequence=sequence_++;chunk.sampleStart=samples_;chunk.samples=frames;chunk.bytes=11+(frames+1)/2;chunk.data[0]=0x42;
   for(unsigned i=0;i<4;++i)chunk.data[1+i]=uint8_t(chunk.sequence>>(8*i));chunk.data[5]=uint8_t(codec_.predictor);chunk.data[6]=uint8_t(uint16_t(codec_.predictor)>>8);chunk.data[7]=codec_.index;chunk.data[9]=uint8_t(frames);chunk.data[10]=uint8_t(frames>>8);
   for(size_t i=0;i<frames;++i){uint8_t value=codec_.encode(frame.data[(at+i)*channels+channel_]);if(i&1)chunk.data[11+i/2]|=value<<4;else chunk.data[11+i/2]=value;}
   bool notify=!buffer_.queued();samples_+=frames;at+=frames;buffer_.push(chunk);if(sink_)sink_(id_,chunk);if(storage_)if(auto e=storage_->push(id_,chunk)){finishLocked(e);return;}
   if(notify&&buffer_.queued()){JsonDocument d;d["recording_id"]=id_;core_.emit(path_,"readable",d.as<JsonObjectConst>(),owner_);}
   if(samples_>=limit){finishLocked(nullptr);return;}
  }

 }
 void describe(JsonObject o)const{if(storage_)storage_->describe(o["storage"].to<JsonObject>());o["encodings"]="ima-adpcm-block";o["readiness"]="started_after_first_nonempty_input_frame";o["events"]="started,readable,stopped,failed";o["input_roles"]=roles_;o["recording_channels"]=1;o["channel_selection"]="start.channel indexes input_roles";o["maximum_block_samples"]=160;RecordingBuffer<>::describeDelivery(o);o["buffer_policy"]="caller-sized compressed RAM ring; oldest blocks discarded with explicit loss";o["buffer_scope"]="recording";o["settings"]["delivery_ttl_ms"]="uint32; zero follows runtime lifetime, positive retains RAM delivery after runtime exit until acknowledgement/release or expiry";asyncContract(o,"start",path_,{"stopped","failed"})["recording_id"]="recording_id";}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(!strcmp(m,"read")||!strcmp(m,"read_chunk")||!strcmp(m,"read_batch")||!strcmp(m,"ack")||!strcmp(m,"release")||!strcmp(m,"list"))return buffer_.invoke(m,a,o,core_.callerOwner());
  std::lock_guard<std::mutex>lock(mutex_);
  if(!strcmp(m,"stop")){if(a.size()>1||a.size()&&!a.containsKey("recording_id"))return "unknown_mic_argument";if(a.containsKey("recording_id")&&a["recording_id"]!=id_)return "recording_replaced";if(core_.callerOwner()&&core_.callerOwner()!=owner_)return "mic_owned_by_other";stop_=true;return nullptr;}
  if(strcmp(m,"start"))return "unsupported_method";if(recording_)return "mic_busy";
  for(auto f:a)if(!Core::containsMethod("sample_rate,channel,buffer_blocks,max_ms,delivery_ttl_ms",f.key().c_str())&&(!storage_||!Core::containsMethod(storage_->fields(),f.key().c_str())))return "unknown_mic_argument";if(storage_)if(auto e=storage_->validate(a))return e;for(auto key:{"sample_rate","channel","buffer_blocks","max_ms","delivery_ttl_ms"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "mic_integer_required";
  unsigned rate=a["sample_rate"]|16000u,channel=a["channel"]|0u;size_t blocks=a["buffer_blocks"]|64u;uint32_t limit=a["max_ms"]|0u;if(channel>=strlen(roles_))return "capture_channel_unavailable";
  char ident[40];snprintf(ident,sizeof ident,"%s-%u",boot_,next_++);if(auto e=buffer_.begin(ident,core_.callerOwner(),rate,blocks,limit,a["storage"]|"off",a["delivery_ttl_ms"]|0u,core_.now()))return e;
  if(storage_)if(auto e=storage_->begin(ident,core_.callerOwner(),rate,a)){buffer_.abandon();return e;}
  if(auto e=begin_(rate)){if(storage_)storage_->finish(ident,e);buffer_.abandon();return e;}copy(id_,sizeof id_,ident);owner_=core_.callerOwner();rate_=rate;channel_=channel;maxMs_=limit;sequence_=0;samples_=0;codec_={};recording_=true;stop_=started_=false;o["recording_id"]=id_;o["state"]="starting";o["channel"]=channel_;o["encoding"]="ima-adpcm-block";return nullptr;
 }
};
}
