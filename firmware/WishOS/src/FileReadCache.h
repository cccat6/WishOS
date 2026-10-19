#pragma once
#include "Vfs.h"
#include <algorithm>
namespace notesos {
// Small random-access windows for parsers such as fonts and image decoders.
// A caller holds its normal VFS read lease; replacement and unavailable media
// invalidate cached bytes rather than turning the cache into a volume authority.
template<size_t Windows=4,size_t Bytes=256>class FileReadCache {
 struct Window{uint64_t base=0;size_t bytes=0;bool valid=false;uint8_t data[Bytes];};
 FileProvider&provider_;const char*path_;Window windows_[Windows];uint8_t order_[Windows]{};uint64_t revision_=UINT64_MAX;
public:
 FileReadCache(FileProvider&provider,const char*path):provider_(provider),path_(path){for(unsigned i=0;i<Windows;++i)order_[i]=i;}
 const char*read(uint64_t offset,uint8_t*out,size_t size,size_t&got){got=0;if(!provider_.available())return "provider_unavailable";auto revision=provider_.changeRevision();if(revision!=revision_){for(auto&w:windows_)w.valid=false;revision_=revision;}
  while(got<size){auto base=(offset/Bytes)*Bytes;unsigned rank=0;while(rank<Windows&&(!windows_[order_[rank]].valid||windows_[order_[rank]].base!=base))++rank;unsigned index=rank<Windows?order_[rank]:order_[Windows-1];auto&w=windows_[index];
   // Large cache misses are streaming reads, not random parser lookups. Read
   // directly into the caller's buffer instead of splitting every image row
   // into small windows and copying it twice. Existing window hits still win.
   if(rank==Windows&&size-got>=Bytes){size_t bytes=0;if(auto e=provider_.read(path_,offset,out+got,size-got,bytes))return e;got+=bytes;offset+=bytes;if(!bytes)return nullptr;continue;}
   if(rank==Windows){size_t bytes=0;while(bytes<Bytes){size_t part=0;if(auto e=provider_.read(path_,base+bytes,w.data+bytes,Bytes-bytes,part)){w.valid=false;return e;}bytes+=part;if(!part)break;}w.base=base;w.bytes=bytes;w.valid=true;rank=Windows-1;}
   for(unsigned i=rank;i>0;--i)order_[i]=order_[i-1];order_[0]=index;size_t at=size_t(offset-base);if(at>=w.bytes)break;auto n=std::min(size-got,w.bytes-at);memcpy(out+got,w.data+at,n);got+=n;offset+=n;
  }return nullptr;
 }
};
}
