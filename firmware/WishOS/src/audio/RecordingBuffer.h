#pragma once
#include "../Core.h"
#include "../Vfs.h"
#include <mutex>
#include <algorithm>
namespace notesos::audio {
// RAM-only delivery buffers. Each recording has an independent identity and
// acknowledgement cursor; beginning another recording cannot erase its tail.
// Persistence is a separate optional sink, never an implicit Flash fallback.
template<size_t PacketBytes=91,size_t SessionLimit=4>class RecordingBuffer {
public:
 struct Chunk{uint32_t sequence=0;uint16_t samples=0,bytes=0;uint64_t sampleStart=0;uint8_t data[PacketBytes]{};};
private:
 struct Session{char id[40]{},file[256]{},storageError[80]{},captureError[80]{},storagePolicy[16]{};uint32_t maxMs=0,owner=0,rate=0,serial=0,next=0,offered=0,acknowledged=0;uint64_t produced=0,dropped=0,storageDropped=0;size_t capacity=0,head=0,count=0;Chunk*blocks=nullptr;bool recording=false,storage=false,storagePending=false,durable=false,orphan=false;};
 ResourceBudget&memory_;mutable std::mutex mutex_;Session sessions_[SessionLimit];Session*current_=nullptr;uint32_t next_=1;
 Session*find(const char*id){for(auto&s:sessions_)if(*s.id&&!strcmp(id,s.id))return &s;return nullptr;}
 void clear(Session&s){if(current_==&s)current_=nullptr;memory_.freeAllocation(s.blocks);s={};}
 void trim(Session&s){if(s.recording)return;if(s.orphan&&!s.storagePending){clear(s);return;}if(!s.count){memory_.freeAllocation(s.blocks);s.blocks=nullptr;s.capacity=s.head=0;}
  else if(s.count<s.capacity/2){auto smaller=static_cast<Chunk*>(memory_.allocate(s.count*sizeof(Chunk)));if(smaller){for(size_t i=0;i<s.count;++i)smaller[i]=s.blocks[(s.head+i)%s.capacity];memory_.freeAllocation(s.blocks);s.blocks=smaller;s.capacity=s.count;s.head=0;}}
 }
 static void base64(const uint8_t*p,size_t n,char*out){static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t j=0;for(size_t i=0;i<n;i+=3){uint32_t bits=uint32_t(p[i])<<16;if(i+1<n)bits|=uint32_t(p[i+1])<<8;if(i+2<n)bits|=p[i+2];out[j++]=table[(bits>>18)&63];out[j++]=table[(bits>>12)&63];out[j++]=i+1<n?table[(bits>>6)&63]:'=';out[j++]=i+2<n?table[bits&63]:'=';}out[j]=0;}
 static void describe(const Session&s,JsonObject o){o["recording_id"]=s.id;o["owner"]=s.owner;o["recording"]=s.recording;o["sample_rate"]=s.rate;o["max_ms"]=s.maxMs;o["storage_requested"]=s.storagePolicy;o["encoding"]="ima-adpcm";o["buffered_chunks"]=s.count;o["buffer_blocks"]=s.capacity;o["next_sequence"]=s.next;o["acknowledged_sequence"]=s.acknowledged;sizeText(o,"produced_samples",s.produced);sizeText(o,"dropped_samples",s.dropped);o["capture_failed"]=bool(*s.captureError);o["capture_error"]=s.captureError;o["file"]=s.file;o["storage_active"]=s.storage&&s.storagePending;o["storage_pending"]=s.storagePending;o["storage_error"]=s.storageError;o["durable"]=s.durable;sizeText(o,"storage_dropped_samples",s.storageDropped);}
public:
 explicit RecordingBuffer(ResourceBudget&m):memory_(m){}
 ~RecordingBuffer(){for(auto&s:sessions_)clear(s);}
 const char*begin(const char*id,uint32_t owner,unsigned rate,size_t blocks,uint32_t maxMs=0,const char*storagePolicy="off"){std::lock_guard<std::mutex>lock(mutex_);
  if(current_&&current_->recording)return "mic_busy";if(!*id||strlen(id)>=sizeof sessions_[0].id||find(id))return "recording_identity";
  Session*slot=nullptr;for(auto&s:sessions_)if(!*s.id){slot=&s;break;}
  if(!slot)for(auto&s:sessions_)if(!s.recording&&!s.count&&!s.storagePending&&(!slot||s.serial<slot->serial))slot=&s;
  if(!slot)return "recording_sessions_full";if(blocks>SIZE_MAX/sizeof(Chunk))return "audio_buffer_range";
  auto buffer=blocks?static_cast<Chunk*>(memory_.allocate(blocks*sizeof(Chunk))):nullptr;if(blocks&&!buffer)return "audio_memory_budget";
  clear(*slot);copy(slot->id,sizeof slot->id,id);slot->owner=owner;slot->rate=rate;slot->maxMs=maxMs;copy(slot->storagePolicy,sizeof slot->storagePolicy,storagePolicy);slot->serial=next_++;slot->capacity=blocks;slot->blocks=buffer;slot->recording=true;current_=slot;return nullptr;
 }
 uint32_t push(const Chunk&chunk){std::lock_guard<std::mutex>lock(mutex_);auto*s=current_;if(!s||!s->recording)return 0;s->produced=chunk.sampleStart+chunk.samples;s->next=chunk.sequence+1;if(!s->capacity)return 0;uint32_t loss=0;
  if(s->count==s->capacity){loss=s->blocks[s->head].samples;s->dropped+=loss;s->head=(s->head+1)%s->capacity;--s->count;}
  s->blocks[(s->head+s->count)%s->capacity]=chunk;++s->count;return loss;
 }
 void finish(const char*error=nullptr){std::lock_guard<std::mutex>lock(mutex_);if(!current_)return;current_->recording=false;if(error)copy(current_->captureError,sizeof current_->captureError,error);trim(*current_);}
 void abandon(){std::lock_guard<std::mutex>lock(mutex_);if(current_)clear(*current_);}
 void storage(const char*id,const char*file,bool pending,bool durable,const char*error,uint64_t missing=0){std::lock_guard<std::mutex>lock(mutex_);auto*s=find(id);if(!s)return;s->storage=true;s->storagePending=pending;s->durable=durable;s->storageDropped=missing;if(file)copy(s->file,sizeof s->file,file);copy(s->storageError,sizeof s->storageError,error?error:"");trim(*s);}
 size_t queued()const{std::lock_guard<std::mutex>lock(mutex_);return current_?current_->count:0;}
 void discardOwner(uint32_t owner){std::lock_guard<std::mutex>lock(mutex_);for(auto&s:sessions_)if(s.owner==owner){s.orphan=true;if(!s.recording&&!s.storagePending)clear(s);}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o,uint32_t owner){std::lock_guard<std::mutex>lock(mutex_);
  if(!strcmp(m,"list")){if(a.size())return "unknown_mic_argument";auto list=o["recordings"].to<JsonArray>();for(auto&s:sessions_)if(*s.id&&(!owner||owner==s.owner)){auto item=list.add<JsonObject>();item["recording_id"]=s.id;item["recording"]=s.recording;item["buffered_chunks"]=s.count;item["storage_pending"]=s.storagePending;}o["session_capacity"]=SessionLimit;return nullptr;}
  for(auto f:a)if(strcmp(f.key().c_str(),"recording_id")&&(!strcmp(m,"read_chunk")?(strcmp(f.key().c_str(),"limit")&&strcmp(f.key().c_str(),"after_sequence")):!strcmp(m,"ack")?strcmp(f.key().c_str(),"next_sequence"):true))return "unknown_mic_argument";
  if(a.containsKey("recording_id")&&(!a["recording_id"].is<const char*>()||a["recording_id"].as<JsonString>().size()!=strlen(a["recording_id"].as<const char*>())))return "recording_id_type";
  auto*s=a.containsKey("recording_id")?find(a["recording_id"]):current_;
  if(!s){if(!strcmp(m,"read")&&!a.size()){o["recording"]=false;o["buffered_chunks"]=0;return nullptr;}return "recording_not_found";}
  if(owner&&s->owner!=owner)return "mic_owned_by_other";
  if(!strcmp(m,"release")){if(s->recording||s->storagePending)return "recording_in_use";clear(*s);return nullptr;}
  if(!strcmp(m,"read_chunk")&&a.containsKey("limit")&&(!a["limit"].is<unsigned>()||a["limit"].as<unsigned>()<1||a["limit"].as<unsigned>()>16))return "audio_read_limit";
  if(!strcmp(m,"ack")||(!strcmp(m,"read_chunk")&&a.containsKey("after_sequence"))){auto value=a[!strcmp(m,"ack")?"next_sequence":"after_sequence"];if(!value.is<uint32_t>()||value.as<uint32_t>()>s->offered)return "audio_ack_range";uint32_t next=value;
   while(s->count&&s->blocks[s->head].sequence<next){s->head=(s->head+1)%s->capacity;--s->count;}if(next>s->acknowledged)s->acknowledged=next;trim(*s);
  }
  if(!strcmp(m,"read_chunk")){
   if(a.containsKey("limit")&&!a["limit"].is<unsigned>())return "audio_read_limit";unsigned limit=a["limit"]|4u;if(!limit||limit>16)return "audio_read_limit";
   describe(*s,o);auto array=o["chunks"].to<JsonArray>();for(unsigned i=0;i<limit&&i<s->count;++i){auto&chunk=s->blocks[(s->head+i)%s->capacity];char encoded[((PacketBytes+2)/3)*4+1];base64(chunk.data,chunk.bytes,encoded);auto item=array.add<JsonObject>();item["sequence"]=chunk.sequence;item["samples"]=chunk.samples;sizeText(item,"sample_start",chunk.sampleStart);item["adpcm_b64"]=encoded;if(measureJson(o)>2800){array.remove(array.size()-1);break;}s->offered=std::max(s->offered,chunk.sequence+1);}
   o["offered_sequence"]=s->offered;
  }else if(strcmp(m,"read")&&strcmp(m,"ack"))return "unsupported_method";
  describe(*s,o);return nullptr;
 }
};
}
