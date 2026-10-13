#pragma once
#include "ResourceBudget.h"
#include <string.h>
namespace notesos {
// Lossless old-pixel state, independently encoded per 16-column band. Empty and
// sparse UI frames need far less RAM than a permanently allocated 43.2 KiB map.
class E6MonoCache {
 struct Band{uint8_t*data=nullptr;uint16_t bytes=0;bool raw=false;} bands_[30];
 ResourceBudget&memory_;
public:
 static constexpr size_t BAND_BYTES=1440;
 explicit E6MonoCache(ResourceBudget&memory):memory_(memory){}
 ~E6MonoCache(){clear();}
 void clear(){for(auto&b:bands_){memory_.freeAllocation(b.data);b={};}}
 size_t bytes()const{size_t n=0;for(auto&b:bands_)n+=b.bytes;return n;}
 void read(unsigned index,uint8_t*out)const{
  const auto&b=bands_[index];memset(out,0,BAND_BYTES);if(!b.data)return;
  if(b.raw){memcpy(out,b.data,BAND_BYTES);return;}
  size_t bit=0;for(unsigned i=0;i<b.bytes;++i){unsigned n=(b.data[i]&127)+1;bool value=b.data[i]&128;while(n--){if(value)out[bit/8]|=1u<<(7-bit%8);++bit;}}
 }
 bool write(unsigned index,const uint8_t*bits){
  uint8_t encoded[BAND_BYTES];size_t size=0,at=0;bool any=false;
  while(at<BAND_BYTES*8&&size<BAND_BYTES){bool value=(bits[at/8]>>(7-at%8))&1;any|=value;unsigned length=1;while(length<128&&at+length<BAND_BYTES*8&&bool((bits[(at+length)/8]>>(7-(at+length)%8))&1)==value)++length;encoded[size++]=uint8_t((value?128:0)|(length-1));at+=length;}
  bool raw=at<BAND_BYTES*8||size>=BAND_BYTES;const uint8_t*source=raw?bits:encoded;if(raw)size=BAND_BYTES;
  if(!any&&at==BAND_BYTES*8){memory_.freeAllocation(bands_[index].data);bands_[index]={};return true;}
  auto data=static_cast<uint8_t*>(memory_.allocate(size));if(!data)return false;memcpy(data,source,size);
  memory_.freeAllocation(bands_[index].data);bands_[index]={data,uint16_t(size),raw};return true;
 }
};
}
