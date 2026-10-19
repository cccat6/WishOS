#pragma once
#include <atomic>
#include "../NativeOperation.h"
#include "../RecordingRetention.h"
#include "RecordingBuffer.h"
#include <condition_variable>
#include <chrono>
namespace notesos::audio {
// Optional compiled storage sink. Capture only enqueues independent blocks;
// directory scans, retention, writes and finalization belong to its worker.
class CaptureStorage {
public:
 virtual ~CaptureStorage()=default;
 virtual const char*fields()const=0;
 virtual const char*validate(JsonObjectConst)=0;
 virtual const char*begin(const char*,uint32_t,unsigned,JsonObjectConst)=0;
 virtual const char*push(const char*,const RecordingBuffer<>::Chunk&)=0;
 virtual void finish(const char*,const char*)=0;
 virtual void poll()=0;
 virtual void describe(JsonObject)const=0;
};
class RecordingStorage final:public CaptureStorage {
 using Chunk=RecordingBuffer<>::Chunk;
 Core&core_;Vfs&vfs_;FileProvider&files_;void*observer_;void(*observe_)(void*,const char*,const char*,bool,bool,const char*,uint64_t);
 std::mutex mutex_;std::condition_variable changed_;Chunk*queue_=nullptr;size_t capacity_=0,head_=0,count_=0;
 bool enabled_=false,ended_=false,stopOnError_=false,durable_=false,oldest_=false;uint32_t owner_=0,pin_=0,rate_=0,ioTimeout_=5000,maxCount_=0;
 uint64_t maxBytes_=0,missing_=0,produced_=0,dataBytes_=0,blocks_=0,order_=0;char id_[40]{},directory_[144]="/recordings",path_[192]{},captureError_[80]{};std::atomic<const char*>error_{nullptr};
 // Bounded batch/path workspace follows the registered memory backend,
 // leaving the native worker stack for filesystem/SDK call chains.
 struct Workspace {Chunk batch[8];uint8_t data[8*91],entries[8*32],header[16];char text[1024],index[208],metadata[208],paths[3][256],filename[64],directory[144];};
 Workspace*workspace_=nullptr;
 void freeBuffers(){core_.resources().freeAllocation(queue_);queue_=nullptr;if(workspace_)workspace_->~Workspace();core_.resources().freeAllocation(workspace_);workspace_=nullptr;}
 NativeOperation operation_;
 static uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
 static void le(uint8_t*p,uint64_t v,unsigned n){while(n--){*p++=uint8_t(v);v>>=8;}}
 template<class Work>const char*io(Work work,const std::atomic<bool>&cancel){auto end=core_.now()+ioTimeout_;for(;;){auto e=work();if(!e||!Core::containsMethod("bus_busy,spi_busy,resource_busy",e))return e;if(cancel)return "cancelled";if(core_.now()>=end)return "recording_io_timeout";std::unique_lock<std::mutex>lock(mutex_);changed_.wait_for(lock,std::chrono::milliseconds(2));}}
 static bool childPath(const char*directory,const char*name,char*out,size_t capacity){return snprintf(out,capacity,"%s%s%s",directory,strcmp(directory,"/")?"/":"",name)<int(capacity);}
 static const char*directoryArgument(JsonObjectConst a,char*out,size_t capacity){
  auto value=a["storage_directory"];if(a.containsKey("storage_directory")&&(!value.is<const char*>()||!*value.as<const char*>()||value.as<JsonString>().size()!=strlen(value.as<const char*>())))return "recording_storage_type";
  return normalizePath("/",value|"/recordings",out,capacity)?nullptr:"invalid_recording_directory";
 }
 const char*prepareDirectory(const std::atomic<bool>&cancel){
  auto&current=workspace_->directory;copy(current,sizeof current,directory_);
  for(char*p=current+1;;++p)if(*p=='/'||!*p){char saved=*p;*p=0;auto&full=workspace_->paths[0];if(!files_.absolutePath(current,full,sizeof full))return "invalid_recording_directory";
   FileInfo info;const char*error;{Vfs::Access lease(vfs_,false,false);if(auto e=lease.add(full,files_.caseInsensitive()))return e;error=io([&]{return files_.stat(current,info);},cancel);}
   if(error){if(strcmp(error,"not_found"))return error;Vfs::Access lease(vfs_,true);if(auto e=lease.add(full,files_.caseInsensitive()))return e;
    if(auto e=io([&]{return files_.stat(current,info);},cancel)){if(strcmp(e,"not_found"))return e;if(auto e=io([&]{return files_.mkdir(current);},cancel))return e;}else if(!info.directory)return "not_directory";
   }else if(!info.directory)return "not_directory";
   *p=saved;if(!saved)break;}
  return nullptr;
 }
 const char*catalog(RecordingRetention&retention,const std::atomic<bool>&cancel){
  auto&directory=workspace_->paths[0];if(!files_.absolutePath(directory_,directory,sizeof directory))return "invalid_recording_path";
  Vfs::Access scan(vfs_,false);if(auto e=scan.add(directory,files_.caseInsensitive()))return e;
  uint32_t cursor=0;
  for(;;){if(cancel)return "cancelled";JsonDocument page;if(auto e=io([&]{return files_.list(directory_,cursor,12,page.to<JsonObject>());},cancel))return e;
   for(auto f:page["entries"].as<JsonArrayConst>()){const char*name=f["name"]|"";size_t length=strlen(name);if(length<11||strcmp(name+length-11,".adpcm.json"))continue;
    auto&metaPath=workspace_->paths[1];if(!childPath(directory_,name,metaPath,sizeof metaPath))continue;auto&text=workspace_->text;size_t got=0;
    if(auto e=io([&]{return files_.read(metaPath,0,reinterpret_cast<uint8_t*>(text),sizeof text,got);},cancel))return e;JsonDocument meta;if(deserializeJson(meta,text,got)||meta["format"]!="notes-recording/1")continue;
    auto&expected=workspace_->paths[2];auto&filename=workspace_->filename;if(snprintf(filename,sizeof filename,"%s.adpcm.json",meta["id"]|"")>=int(sizeof filename)||!childPath(directory_,filename,expected,sizeof expected)||strcmp(expected,metaPath))continue;
    metaPath[strlen(metaPath)-5]=0;FileInfo data,index;auto&indexPath=workspace_->paths[2];snprintf(indexPath,sizeof indexPath,"%s.idx",metaPath);
    if(auto e=io([&]{return files_.stat(metaPath,data);},cancel))return e;if(auto e=io([&]{return files_.stat(indexPath,index);},cancel))return e;
    if(auto e=retention.remember(metaPath,got+data.size+index.size,meta["order"]|uint64_t(0)))return e;
   }
   if(page["more"]!=true)return nullptr;cursor=page["next_cursor"];
  }
 }
 const char*store(const std::atomic<bool>&cancel){
  if(auto e=prepareDirectory(cancel))return e;
  RecordingRetention retention(files_,vfs_,&core_.resources(),directory_);retention.configure({maxBytes_,maxCount_,oldest_});if(auto e=catalog(retention,cancel))return e;order_=retention.nextOrder();
  if(auto e=retention.makeRoom(1024,1))return e;
  auto&index=workspace_->index;auto&metadata=workspace_->metadata;snprintf(index,sizeof index,"%s.idx",path_);snprintf(metadata,sizeof metadata,"%s.json",path_);
  Vfs::Access lease(vfs_,true);for(auto p:{path_,index,metadata}){auto&full=workspace_->paths[0];if(!files_.absolutePath(p,full,sizeof full))return "invalid_recording_path";if(auto e=lease.add(full,files_.caseInsensitive()))return e;}
  if(auto e=io([&]{return files_.createFile(path_);},cancel))return e;if(auto e=io([&]{return files_.createFile(index);},cancel))return e;
  auto&header=workspace_->header;memset(header,0,sizeof header);memcpy(header,"NAI1",4);le(header+4,rate_,4);le(header+8,32,4);size_t wrote=0;
  if(auto e=io([&]{return files_.write(index,0,header,sizeof header,false,wrote);},cancel))return e;if(wrote!=sizeof header)return "recording_short_write";
  for(;;){auto&batch=workspace_->batch;size_t count=0;{std::unique_lock<std::mutex>lock(mutex_);if(cancel&&!ended_){ended_=true;copy(captureError_,sizeof captureError_,"cancelled");}if(!count_&&!ended_){changed_.wait_for(lock,std::chrono::milliseconds(10));continue;}
    while(count_&&count<8){batch[count++]=queue_[head_];head_=(head_+1)%capacity_;--count_;}if(!count&&ended_)break;}
   auto&data=workspace_->data;auto&entries=workspace_->entries;memset(entries,0,sizeof entries);size_t bytes=0;for(size_t i=0;i<count;++i){auto&b=batch[i];auto p=entries+i*32;le(p,b.sequence,4);le(p+4,b.sampleStart,8);le(p+12,dataBytes_+bytes,8);le(p+20,b.bytes,4);le(p+24,crc(b.data,b.bytes),4);memcpy(data+bytes,b.data,b.bytes);bytes+=b.bytes;}
   const char*failure=error_.load();if(!failure)failure=retention.makeRoom(dataBytes_+bytes+(blocks_+count)*32+1024,1);
   if(!failure){failure=io([&]{return files_.write(path_,dataBytes_,data,bytes,false,wrote);},cancel);if(!failure&&wrote!=bytes)failure="recording_short_write";}
   if(!failure){failure=io([&]{return files_.write(index,16+blocks_*32,entries,count*32,false,wrote);},cancel);if(!failure&&wrote!=count*32)failure="recording_short_write";}
   if(failure){error_=failure;std::lock_guard<std::mutex>lock(mutex_);for(size_t i=0;i<count;++i)missing_+=batch[i].samples;}
   else{dataBytes_+=bytes;blocks_+=count;}
  }
  // Only indexed, complete blocks belong to the archive. A partial write cannot
  // become a valid tail or be mistaken for a fully captured recording.
  if(auto e=io([&]{return files_.resize(path_,dataBytes_);},cancel))return e;if(auto e=io([&]{return files_.resize(index,16+blocks_*32);},cancel))return e;
  JsonDocument meta;meta["format"]="notes-recording/1";meta["id"]=id_;meta["order"]=order_;meta["sample_rate_hz"]=rate_;meta["encoding"]="ima-adpcm-blocks";meta["channels"]=1;meta["index"]="nai1-le-32";meta["bytes"]=dataBytes_;meta["blocks"]=blocks_;meta["missing_samples"]=missing_;sizeText(meta.as<JsonObject>(),"produced_samples",produced_);meta["capture_failed"]=bool(*captureError_);meta["capture"]=(error_.load()||missing_||*captureError_)?"incomplete":"finalized";
  auto&text=workspace_->text;size_t n=serializeJson(meta,text,sizeof text);if(n>=sizeof text)return "recording_metadata_limit";
  if(auto e=io([&]{return files_.createFile(metadata);},cancel))return e;if(auto e=io([&]{return files_.write(metadata,0,(const uint8_t*)text,n,false,wrote);},cancel))return e;if(wrote!=n)return "recording_short_write";
  for(auto p:{path_,index,metadata})if(auto e=io([&]{return files_.flush(p);},cancel))return e;durable_=files_.durableWrites();return nullptr;
 }
public:
 RecordingStorage(Core&c,Vfs&v,FileProvider&f,NativeOperation::Executor executor,void*observer,void(*observe)(void*,const char*,const char*,bool,bool,const char*,uint64_t)):
  core_(c),vfs_(v),files_(f),observer_(observer),observe_(observe),operation_(c,nullptr,executor,this,[](void*p,const std::atomic<bool>&cancel)->const char*{auto&s=*static_cast<RecordingStorage*>(p);auto e=s.store(cancel);if(e)s.error_=e;return e;},[](void*p){auto&s=*static_cast<RecordingStorage*>(p);std::lock_guard<std::mutex>lock(s.mutex_);s.enabled_=false;if(s.pin_)s.vfs_.unpin(s.pin_);s.pin_=0;s.freeBuffers();char full[256]{};s.files_.absolutePath(s.path_,full,sizeof full);s.observe_(s.observer_,s.id_,full,false,s.durable_,s.error_.load(),s.missing_);JsonDocument event;event["recording_id"]=s.id_;event["file"]=full;event["durable"]=s.durable_;event["complete"]=s.durable_&&!s.error_.load()&&!s.missing_&&!*s.captureError_;event["missing_samples"]=s.missing_;if(s.error_)event["error"]=s.error_.load();s.core_.emit("/dev/mic",s.error_?"storage_failed":"stored",event.as<JsonObjectConst>(),s.owner_);}){}
 const char*fields()const override{return "storage,storage_directory,storage_required,storage_buffer_blocks,storage_max_bytes,storage_max_count,storage_on_limit,storage_on_failure,storage_timeout_ms";}
 const char*validate(JsonObjectConst a)override{
  char directory[sizeof directory_];if(auto e=directoryArgument(a,directory,sizeof directory))return e;
  for(auto k:{"storage","storage_on_limit","storage_on_failure"})if(a.containsKey(k)&&!a[k].is<const char*>())return "recording_storage_type";
  if(!Core::containsMethod("off,retry,history",a["storage"]|"off"))return "recording_storage_policy";
  if(a.containsKey("storage_required")&&!a["storage_required"].is<bool>())return "recording_storage_type";
  for(auto k:{"storage_buffer_blocks","storage_max_count","storage_timeout_ms"})if(a.containsKey(k)&&!a[k].is<uint32_t>())return "recording_storage_type";
  if((a["storage_buffer_blocks"]|64u)<1||(a["storage_buffer_blocks"]|64u)>SIZE_MAX/sizeof(Chunk))return "recording_storage_budget";
  uint64_t bytes;if(a.containsKey("storage_max_bytes")&&!parseOffset(a["storage_max_bytes"],bytes))return "recording_storage_type";
  if(!Core::containsMethod("stop_storage,delete_oldest",a["storage_on_limit"]|"stop_storage")||!Core::containsMethod("continue,stop_capture",a["storage_on_failure"]|"continue")||!(a["storage_timeout_ms"]|5000u))return "recording_storage_policy";return nullptr;
 }
 const char*begin(const char*id,uint32_t owner,unsigned rate,JsonObjectConst a)override{
  if(a["storage"].isNull()||a["storage"]=="off")return nullptr;if(operation_.active())return "recording_storage_busy";char directory[sizeof directory_];if(auto e=directoryArgument(a,directory,sizeof directory))return e;
  const char*error=nullptr;if(!files_.available())error="recording_storage_unavailable";else if(files_.busyPath("/"))error="storage_preparing";else if(!files_.writable)error="read_only";else if(!files_.persistent||!strcmp(files_.medium,"internal_flash"))error="recording_storage_medium";
  if(!error){capacity_=a["storage_buffer_blocks"]|64u;queue_=static_cast<Chunk*>(core_.resources().allocate(capacity_*sizeof(Chunk)));if(queue_){auto p=core_.resources().allocate(sizeof(Workspace));if(p)workspace_=new(p)Workspace;}if(!queue_||!workspace_){freeBuffers();error="recording_storage_memory_budget";}}
  if(error){observe_(observer_,id,nullptr,false,false,error,0);return a["storage_required"]==true?error:nullptr;}
  copy(id_,sizeof id_,id);copy(directory_,sizeof directory_,directory);char filename[48];snprintf(filename,sizeof filename,"%s.adpcm",id);childPath(directory_,filename,path_,sizeof path_);owner_=owner;rate_=rate;head_=count_=0;enabled_=true;ended_=durable_=false;error_=nullptr;captureError_[0]=0;missing_=produced_=dataBytes_=blocks_=0;parseOffset(a["storage_max_bytes"],maxBytes_);maxCount_=a["storage_max_count"]|0u;oldest_=a["storage_on_limit"]=="delete_oldest";stopOnError_=a["storage_on_failure"]=="stop_capture";ioTimeout_=a["storage_timeout_ms"]|5000u;
  pin_=core_.acquireOwner();if(!vfs_.pin(files_,pin_))error="provider_lease_capacity";
  JsonDocument out;if(!error)error=operation_.start("record",out.to<JsonObject>(),0);
  if(error){if(pin_)vfs_.unpin(pin_);pin_=0;freeBuffers();enabled_=false;observe_(observer_,id,nullptr,false,false,error,0);return a["storage_required"]==true?error:nullptr;}
  char full[256];files_.absolutePath(path_,full,sizeof full);observe_(observer_,id_,full,true,false,nullptr,0);return nullptr;
 }
 const char*push(const char*id,const Chunk&chunk)override{std::lock_guard<std::mutex>lock(mutex_);if(strcmp(id,id_))return nullptr;if(!enabled_||ended_)return stopOnError_?error_.load():nullptr;produced_=chunk.sampleStart+chunk.samples;
  if(count_==capacity_){missing_+=chunk.samples;return stopOnError_?"recording_spool_overflow":nullptr;}else{queue_[(head_+count_)%capacity_]=chunk;++count_;changed_.notify_one();}return stopOnError_?error_.load():nullptr;
 }
 void finish(const char*id,const char*error)override{std::lock_guard<std::mutex>lock(mutex_);if(!enabled_||strcmp(id,id_))return;ended_=true;copy(captureError_,sizeof captureError_,error?error:"");changed_.notify_one();}
 void poll()override{operation_.poll();}
 void describe(JsonObject o)const override{o["provider"]=files_.id;o["policies"]="off,retry,history";o["format"]="notes-recording/1";o["index"]="nai1-le-32";o["retention"]="storage_max_bytes,storage_max_count,storage_on_limit:stop_storage|delete_oldest";o["storage_on_failure"]="continue|stop_capture";o["buffer_blocks"]=64;o["default"]="off";auto directory=o["settings"]["storage_directory"].to<JsonObject>();directory["type"]="string";directory["default"]="/recordings";directory["base"]="selected_provider_root";directory["maximum_bytes"]=sizeof directory_-1;directory["apply_time"]="recording_start";directory["normalization"]="provider_relative_path_cannot_escape_provider";o["internal_flash_fallback"]=false;o["finalization_lifetime"]="system_until_capture_tail_and_metadata_settle";}
};
}
