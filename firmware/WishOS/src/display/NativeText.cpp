#include "NativeText.h"
#include "NativeFont.h"
#include "Lz4.h"
namespace nm {
bool localTextSupported(const char *text,uint8_t size){
 const uint8_t *t=reinterpret_cast<const uint8_t*>(text);
 while(*t){uint32_t cp=*t++;if(cp=='\n')continue;
  if(cp>=0x80){int n=0;uint32_t min=0;
   if(cp>=0xc2&&cp<=0xdf){n=1;min=0x80;cp&=31;}
   else if(cp>=0xe0&&cp<=0xef){n=2;min=0x800;cp&=15;}
   else if(cp>=0xf0&&cp<=0xf4){n=3;min=0x10000;cp&=7;}else return false;
   while(n--){if((*t&0xc0)!=0x80)return false;cp=(cp<<6)|(*t++&63);}
   if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
  }
  bool found=false;for(const auto &g:NATIVE_GLYPHS)if(g.code==cp&&g.size==size){found=true;break;}
  if(!found)return false;
 }
 return true;
}
int nativeTextWidth(const char *text,uint8_t size){
 if(!localTextSupported(text,size))return -1;int width=0;const uint8_t *t=reinterpret_cast<const uint8_t*>(text);
 while(*t){uint32_t cp=*t++;if(cp>=0x80){int n=cp<0xe0?1:cp<0xf0?2:3;cp&=n==1?31:n==2?15:7;while(n--)cp=(cp<<6)|(*t++&63);}
  for(const auto &g:NATIVE_GLYPHS)if(g.code==cp&&g.size==size){width+=g.advance;break;}
 }return width;
}
static const NativeGlyph *glyph(uint32_t code,uint8_t size){
  size_t lo=0,hi=sizeof(NATIVE_GLYPHS)/sizeof(NATIVE_GLYPHS[0]);const uint32_t key=(uint32_t(size)<<21)|code;
  while(lo<hi){const size_t mid=(lo+hi)/2;const auto &g=NATIVE_GLYPHS[mid];const uint32_t k=(uint32_t(g.size)<<21)|g.code;if(k<key)lo=mid+1;else hi=mid;}
  if(lo<sizeof(NATIVE_GLYPHS)/sizeof(NATIVE_GLYPHS[0])&&NATIVE_GLYPHS[lo].code==code&&NATIVE_GLYPHS[lo].size==size)return &NATIVE_GLYPHS[lo];
  return code==0x25a1?nullptr:glyph(0x25a1,size);
}
bool nativeGlyphMetrics(uint32_t cp,uint8_t size,FontMetrics&m){auto g=glyph(cp,size);if(!g)return false;m={g->w,g->h,g->advance,g->x,g->y};return true;}
bool nativeGlyphAlpha(uint32_t cp,uint8_t size,uint8_t*out,size_t stride){auto g=glyph(cp,size);if(!g||stride<g->w)return false;uint8_t unpacked[512];const uint8_t*bits=NATIVE_FONT_BITS+g->offset;size_t row=(g->w+7)/8;
 if(g->stored&0x8000){if(row*g->h>sizeof(unpacked)||!lz4Raw(bits,g->stored&0x7fff,unpacked,row*g->h))return false;bits=unpacked;}
 for(unsigned y=0;y<g->h;++y)for(unsigned x=0;x<g->w;++x)out[y*stride+x]=(bits[y*row+x/8]&(0x80>>(x&7)))?255:0;return true;
}
bool nativeGlyphBitmap(uint32_t cp,uint8_t size,uint8_t*out,size_t capacity,FontMetrics&m){
 auto g=glyph(cp,size);if(!g)return false;size_t bytes=((g->w+7)/8)*g->h;
 if(bytes>capacity)return false;m={g->w,g->h,g->advance,g->x,g->y};const uint8_t*bits=NATIVE_FONT_BITS+g->offset;
 if(g->stored&0x8000)return lz4Raw(bits,g->stored&0x7fff,out,bytes);
 memcpy(out,bits,bytes);return true;
}
}
