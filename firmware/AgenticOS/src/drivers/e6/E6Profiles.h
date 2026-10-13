#pragma once
#include "E6ColorDriver.h"
#include "E6Palette.h"
namespace notesos {
enum class E6Profile:uint8_t {StockUI,StockImage,BlueYellow,ColorFast,ColorQuality};
struct E6ProfileInfo {const char*id;E6Profile profile;unsigned colors;size_t sourceBytes;const char*format;uint32_t estimateMs;const char*guide;};
inline constexpr E6ProfileInfo E6_PROFILES[]={
 {"e6.ink6.ui-v1",E6Profile::StockUI,6,172800,"native720x480-4bpp",16000,"Controller built-in six-color UI"},
 {"e6.ink6.image-v1",E6Profile::StockImage,6,172800,"native720x480-4bpp",30000,"Controller built-in six-color image"},
 {"e6.blue-yellow.v1",E6Profile::BlueYellow,2,43200,"native720x480-1bpp",700,"Menus, selection and frequent text updates; blue ink on yellow"},
 {"e6.color-fast.v1",E6Profile::ColorFast,55,345600,"native720x480-index8",2800,"Colored controls and status; approximate reference palette"},
 {"e6.color-quality.v1",E6Profile::ColorQuality,48,345600,"native720x480-index8",13300,"Illustrations, photographs and color-sensitive pages"}
};
inline const E6ProfileInfo*e6Profile(const char*id){for(auto&p:E6_PROFILES)if(!strcmp(p.id,id))return &p;return nullptr;}
inline const E6ProfileInfo&e6Profile(E6Profile id){return E6_PROFILES[unsigned(id)];}
inline bool e6Extended(E6Profile id){return unsigned(id)>=unsigned(E6Profile::BlueYellow);}
inline bool e6Color(E6Profile id){return id==E6Profile::ColorFast||id==E6Profile::ColorQuality;}
inline E6ColorDriver::Mode e6Mode(E6Profile id){return id==E6Profile::BlueYellow?E6ColorDriver::Mode::BlueYellow:id==E6Profile::ColorFast?E6ColorDriver::Mode::Fast:E6ColorDriver::Mode::Quality;}
inline bool e6ValidCode(E6Profile profile,uint8_t code){
 if(profile==E6Profile::BlueYellow)return code<2;
 if(!e6Color(profile))return code<7&&code!=4;
 if(code>=64)return false;
 constexpr auto mask=[](const auto&codes){uint64_t value=0;for(auto code:codes)value|=uint64_t(1)<<code;return value;};
 constexpr uint64_t fast=mask(e6::tickey_fast_valid_codes),quality=mask(e6::tickey_qual_valid_codes);
 return ((profile==E6Profile::ColorFast?fast:quality)>>code)&1;
}
inline uint8_t e6Ink(uint16_t value,E6Profile profile){
 struct Cached{uint16_t rgb=0;E6Profile profile=E6Profile::StockUI;uint8_t code=0;bool used=false;};static Cached cache[16];static unsigned next=0;
 for(auto&e:cache)if(e.used&&e.rgb==value&&e.profile==profile)return e.code;
 int r=(value>>11)*255/31,g=((value>>5)&63)*255/63,b=(value&31)*255/31,best=INT32_MAX;uint8_t code=0;
 static constexpr uint8_t stockRGB[][3]={{0,0,0},{255,255,255},{255,255,0},{255,0,0},{0,0,255},{0,255,0}},stockCodes[]={0,1,2,3,5,6};
 for(unsigned i=0;i<e6Profile(profile).colors;++i){const uint8_t*rgb;uint8_t candidate;
  if(profile==E6Profile::BlueYellow){candidate=i;rgb=stockRGB[i?4:2];}
  else if(e6Color(profile)){candidate=profile==E6Profile::ColorFast?e6::tickey_fast_valid_codes[i]:e6::tickey_qual_valid_codes[i];rgb=profile==E6Profile::ColorFast?e6::tickey_fast_rgb64[candidate]:e6::tickey_qual_rgb64[candidate];}
  else{candidate=stockCodes[i];rgb=stockRGB[i];}
  int dr=r-rgb[0],dg=g-rgb[1],db=b-rgb[2],distance=dr*dr+dg*dg+db*db;if(distance<best){best=distance;code=candidate;}
 }
 cache[next++%16]={value,profile,code,true};return code;
}
inline uint8_t e6Plane(uint8_t code,E6Profile profile,bool low){return e6Color(profile)?(low?code&7:code>>3):code;}
}
