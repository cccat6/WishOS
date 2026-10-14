#pragma once
#include "Vfs.h"
#include <string.h>
namespace notesos {
// Indexed monochrome glyphs prepared on the host. Runtime files are data, not
// executable font programs; no unbounded TrueType outline parser runs on-device.
class PackedFont {
 FileProvider*provider_=nullptr;char path_[256]{};uint32_t count_=0;uint64_t data_=0,size_=0;
 static uint32_t le(const uint8_t*p,unsigned n){uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(p[i])<<(8*i);return v;}
 const char*read(uint64_t at,uint8_t*out,size_t n)const{if(at>size_||n>size_-at)return "font_range";size_t got=0;if(auto e=provider_->read(path_,at,out,n,got))return e;return got==n?nullptr:"font_short_read";}
public:
 static constexpr unsigned MAX_SIZE=96,MAX_BITMAP_BYTES=1152;
 struct Glyph{uint32_t code=0,index=0,offset=0;uint8_t size=0,w=0,h=0,advance=0;int8_t x=0,y=0;uint16_t bytes=0;};
 const char*open(FileProvider&provider,const char*path){
  FileInfo info;if(auto e=provider.stat(path,info))return e;if(info.directory||info.size<16)return "font_format";
  if(strlen(path)>=sizeof path_)return "font_path";
  provider_=&provider;strcpy(path_,path);size_=info.size;uint8_t header[16];if(auto e=read(0,header,sizeof header))return e;
  if(memcmp(header,"NMF1",4))return "font_requires_compiled_nmf1";count_=le(header+4,4);data_=16+uint64_t(count_)*16;
  if(!count_||data_>size_||le(header+8,4)!=size_-data_)return "font_index_range";return nullptr;
 }
 const char*record(uint32_t index,Glyph&g)const{
  if(index>=count_)return "font_glyph_range";uint8_t bytes[16];if(auto e=read(16+uint64_t(index)*16,bytes,16))return e;
  g={};g.code=le(bytes,4);g.index=index;g.size=bytes[4];g.w=bytes[5];g.h=bytes[6];g.x=int8_t(bytes[7]);g.y=int8_t(bytes[8]);g.advance=bytes[9];g.bytes=le(bytes+10,2);g.offset=le(bytes+12,4);
  if(g.size>MAX_SIZE||g.w>MAX_SIZE||g.h>MAX_SIZE||g.bytes!=((g.w+7)/8)*g.h||g.bytes>MAX_BITMAP_BYTES||g.offset>size_-data_||g.bytes>size_-data_-g.offset)return "font_glyph_range";return nullptr;
 }
 const char*find(uint32_t code,unsigned size,Glyph&glyph)const{
  uint32_t lo=0,hi=count_;while(lo<hi){uint32_t mid=lo+(hi-lo)/2;Glyph g;if(auto e=record(mid,g))return e;if(g.size<size||(g.size==size&&g.code<code))lo=mid+1;else hi=mid;}
  if(lo==count_)return "font_missing_glyph";if(auto e=record(lo,glyph))return e;return glyph.size==size&&glyph.code==code?nullptr:"font_missing_glyph";
 }
 const char*bitmap(const Glyph&glyph,uint8_t*out)const{return read(data_+glyph.offset,out,glyph.bytes);}
};
}
