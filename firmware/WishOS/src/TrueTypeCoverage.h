#pragma once
#include "FileReadCache.h"
namespace notesos {
// Read-only counterpart of the pinned tinyTTF stbtt_FindGlyphIndex. It reads
// cmap windows, never creates a face, glyph cache, renderer or filesystem state.
class TrueTypeCoverage {
 FileProvider&provider_;const char*path_;FileReadCache<4> cache_;
 uint64_t bytes_=0,map_=0,length_=0;unsigned format_=0,segments_=0;uint32_t groups_=0;
 const char*error_=nullptr;
 uint32_t number(uint64_t at,unsigned count){uint8_t data[4]{};if(error_)return 0;if(at>bytes_||count>bytes_-at){error_="font_table_range";return 0;}size_t got=0;error_=cache_.read(at,data,count,got);if(!error_&&got!=count)error_="font_short_read";uint32_t n=0;for(unsigned i=0;i<count;++i)n=(n<<8)|data[i];return n;}
 uint32_t value(uint64_t at,unsigned count){if(at>length_||count>length_-at){error_="font_cmap_range";return 0;}return number(map_+at,count);}
public:
 TrueTypeCoverage(FileProvider&p,const char*path):provider_(p),path_(path),cache_(p,path){}
 const char*open(){FileInfo info;if(auto e=provider_.stat(path_,info))return e;if(info.directory||info.size<12)return "font_format";bytes_=info.size;
  if(number(0,4)!=0x10000)return error_?error_:"font_requires_truetype_outlines";
  auto tables=number(4,2);if(!tables||12+uint64_t(tables)*16>bytes_)return "font_table_range";
  unsigned required=0;uint64_t cmap=0,cmapBytes=0;
  const uint32_t tags[]={0x68656164,0x68686561,0x686d7478,0x6d617870,0x636d6170,0x6c6f6361,0x676c7966};
  for(unsigned i=0;i<tables&&!error_;++i){uint64_t at=12+uint64_t(i)*16;auto tag=number(at,4);uint64_t offset=number(at+8,4),size=number(at+12,4);if(offset>bytes_||size>bytes_-offset)return "font_table_range";for(unsigned j=0;j<7;++j)if(tag==tags[j])required|=1u<<j;if(tag==tags[4]&&!cmap){cmap=offset;cmapBytes=size;}}
  if(error_)return error_;if(required!=127)return "font_required_table";if(cmapBytes<4)return "font_cmap_range";
  auto encodings=number(cmap+2,2);if(4+uint64_t(encodings)*8>cmapBytes)return "font_cmap_range";
  // stb chooses the last supported encoding record, not the first/best format.
  for(unsigned i=0;i<encodings&&!error_;++i){uint64_t at=cmap+4+uint64_t(i)*8;auto platform=number(at,2),encoding=number(at+2,2);if(platform==0||(platform==3&&(encoding==1||encoding==10))){auto offset=number(at+4,4);if(offset>cmapBytes||cmapBytes-offset<2)return "font_cmap_range";map_=cmap+offset;length_=cmapBytes-offset;}}
  if(error_)return error_;if(!map_)return "font_cmap_encoding";format_=value(0,2);
  if(format_!=0&&format_!=4&&format_!=6&&format_!=12&&format_!=13)return "font_cmap_format_not_supported";
  auto declared=value(format_>=12?4:2,format_>=12?4:2);if(error_)return error_;if(declared>length_)return "font_cmap_range";length_=declared;
  if(format_==0){if(length_<6)return "font_cmap_range";}
  else if(format_==6){if(length_<10)return "font_cmap_range";auto count=value(8,2);if(10+uint64_t(count)*2>length_)return "font_cmap_range";}
  else if(format_==4){if(length_<16)return "font_cmap_range";auto twice=value(6,2);segments_=twice/2;if(!segments_||(twice&1)||16+uint64_t(segments_)*8>length_)return "font_cmap_range";
   unsigned power=1,selector=0;while(power*2<=segments_){power*=2;++selector;}if(value(8,2)!=power*2||value(10,2)!=selector||value(12,2)!=(segments_-power)*2)return "font_cmap_range";
   uint32_t last=0;for(unsigned i=0;i<segments_&&!error_;++i){auto end=value(14+2*i,2),start=value(16+2*segments_+2*i,2);if((i&&end<=last)||start>end)return "font_cmap_range";last=end;}}
  else{if(length_<16)return "font_cmap_range";groups_=value(12,4);if(16+uint64_t(groups_)*12>length_)return "font_cmap_range";}
  return error_;
 }
 const char*glyph(uint32_t cp,uint32_t&index){index=0;if(error_)return error_;
  if(format_==0){if(cp<length_-6)index=value(6+cp,1);}
  else if(format_==6){auto first=value(6,2),count=value(8,2);if(cp>=first&&cp-first<count)index=value(10+uint64_t(cp-first)*2,2);}
  else if(format_==4){if(cp>0xffff)return nullptr;unsigned lo=0,hi=segments_;while(lo<hi&&!error_){unsigned mid=lo+(hi-lo)/2;if(value(14+2*mid,2)<cp)lo=mid+1;else hi=mid;}if(lo<segments_){auto start=value(16+2*segments_+2*lo,2);if(cp>=start){auto field=16+6*segments_+2*lo;auto offset=value(field,2);if(offset)index=value(field+uint64_t(offset)+2*(cp-start),2);else index=uint16_t(cp+int16_t(value(16+4*segments_+2*lo,2)));}}}
  else{uint32_t lo=0,hi=groups_;while(lo<hi&&!error_){auto mid=lo+(hi-lo)/2;uint64_t at=16+uint64_t(mid)*12;auto start=value(at,4),end=value(at+4,4);if(start>end){error_="font_cmap_range";break;}if(cp<start)hi=mid;else if(cp>end)lo=mid+1;else{auto first=value(at+8,4);uint64_t result=uint64_t(first)+(format_==12?cp-start:0);if(result>UINT32_MAX)error_="font_cmap_range";else index=uint32_t(result);break;}}}
  return error_;
 }
 unsigned format()const{return format_;}
 static bool zeroWidth(uint32_t cp){return cp<0x20||cp==0xf8ff||cp==0x200c;}
};
}
