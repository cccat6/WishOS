#pragma once
#include "Vfs.h"
#include "Core.h"
#include <algorithm>
namespace notesos {
struct RecordingPolicy {
 uint64_t maxBytes=0;uint32_t maxCount=0;bool oldest=false;
};
// The index contains only system-owned recordings; arbitrary user files are never deletion candidates.
class RecordingRetention {
 struct Entry{char path[192]{};uint64_t bytes=0,order=0;};Entry*entries_=nullptr;size_t count_=0,capacity_=0;uint64_t order_=0;
 FileProvider&files_;Vfs&vfs_;ResourceBudget*memory_;RecordingPolicy policy_;uint32_t removed_=0;
 const char*remove(size_t index){Vfs::Access lease(vfs_,true);auto&e=entries_[index];char full[256],path[256];for(auto suffix:{"",".idx",".json"}){snprintf(path,sizeof path,"%s%s",e.path,suffix);if(!files_.absolutePath(path,full,sizeof full))return "invalid_recording_path";if(lease.add(full,files_.caseInsensitive())||files_.busyPath(path))return "recording_in_use";}
   for(auto suffix:{".json",".idx",""}){snprintf(path,sizeof path,"%s%s",e.path,suffix);auto error=files_.remove(path);if(error&&strcmp(error,"not_found"))return error;}
   for(size_t n=index+1;n<count_;++n)entries_[n-1]=entries_[n];--count_;entries_[count_]={};++removed_;return nullptr;
 }
public:
 RecordingRetention(FileProvider&f,Vfs&v,ResourceBudget*memory=nullptr):files_(f),vfs_(v),memory_(memory){}
 ~RecordingRetention(){if(memory_)memory_->freeAllocation(entries_);else free(entries_);}
 uint64_t nextOrder()const{uint64_t order=0;for(size_t n=0;n<count_;++n)if(entries_[n].order>order)order=entries_[n].order;return order+1;}
 void configure(RecordingPolicy p){policy_=p;}
 uint64_t bytes()const{uint64_t total=0;for(size_t n=0;n<count_;++n)total+=entries_[n].bytes;return total;}
 const char*remember(const char*path,uint64_t bytes,uint64_t order=0){if(strncmp(path,"/recordings/",12)||strlen(path)>=192||strlen(path)<6||strcmp(path+strlen(path)-6,".adpcm"))return "invalid_recording_path";for(size_t n=0;n<count_;++n)if(!strcmp(entries_[n].path,path)){entries_[n].bytes=bytes;return nullptr;}if(count_==capacity_){if(capacity_>SIZE_MAX/sizeof(Entry)-8)return "recording_catalog_limit";size_t capacity=capacity_+8;auto next=(Entry*)(memory_?memory_->reallocate(entries_,capacity*sizeof(Entry)):realloc(entries_,capacity*sizeof(Entry)));if(!next)return "recording_index_memory_low";entries_=next;capacity_=capacity;}auto&e=entries_[count_++];e={};strcpy(e.path,path);e.bytes=bytes;e.order=order?order:++order_;return nullptr;}
 const char*makeRoom(uint64_t additionalBytes,uint32_t additionalCount=0){
   if(policy_.maxBytes&&additionalBytes>policy_.maxBytes)return "recording_space_limit";
   while((policy_.maxBytes&&bytes()>policy_.maxBytes-additionalBytes)||(policy_.maxCount&&count_+additionalCount>policy_.maxCount)){
     if(!policy_.oldest)return "recording_retention_limit";if(!count_)return "recording_retention_limit";size_t oldest=0;for(size_t n=1;n<count_;++n)if(entries_[n].order<entries_[oldest].order)oldest=n;
     if(auto e=remove(oldest))return e;
   }return nullptr;
 }
 void read(JsonObject o)const{o["count"]=count_;sizeText(o,"bytes",bytes());o["catalog_allocated_entries"]=capacity_;o["catalog_limit"]="available_memory";o["removed"]=removed_;sizeText(o,"max_bytes",policy_.maxBytes);o["max_count"]=policy_.maxCount;o["on_limit"]=policy_.oldest?"delete_oldest":"stop_storage";}
};
}
