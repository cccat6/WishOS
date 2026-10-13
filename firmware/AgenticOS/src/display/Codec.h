#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace nm {
inline bool color(uint8_t c) {
  return c == 0 || c == 1 || c == 2 || c == 3 || c == 5 || c == 6;
}
inline uint16_t u16(const uint8_t *p) {
  return p[0] | uint16_t(p[1]) << 8;
}
inline uint32_t u32(const uint8_t *p) {
  return u16(p) | uint32_t(u16(p + 2)) << 16;
}
inline uint32_t crc32(const uint8_t *p, size_t n) {
  uint32_t c = 0xffffffffu;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; i++)
      c = (c >> 1) ^ (0xedb88320u & uint32_t(-int(c & 1)));
  }
  return ~c;
}
// NMR1: magic, uint16 LE width/height, uint32 LE record CRC, then <uint16 run,uint8 color>.
inline bool validRle(const uint8_t *p, size_t n) {
  if (n < 15 || memcmp(p, "NMR1", 4) || u16(p + 4) != 480 || u16(p + 6) != 720 || (n - 12) % 3)
    return false;
  if (crc32(p + 12, n - 12) != u32(p + 8))
    return false;
  uint32_t pixels = 0;
  for (size_t i = 12; i < n; i += 3) {
    auto count = u16(p + i);
    if (!count || !color(p[i + 2]))
      return false;
    pixels += count;
    if (pixels > 480u * 720u)
      return false;
  }
  return pixels == 480u * 720u;
}
struct RleReader {
  const uint8_t *p;
  size_t at = 12;
  uint16_t left = 0;
  uint8_t value = 1;
  explicit RleReader(const uint8_t *data) : p(data) {}
  // Caller validates the entire NMR1 first. Skip runs without expanding unused pixels.
  void skip(uint32_t count) {
    while (count) {
      if (!left) {
        left = u16(p + at);
        value = p[at + 2];
        at += 3;
      }
      const uint32_t step = count < left ? count : left;
      left -= step;
      count -= step;
    }
  }
  uint8_t next() {
    if (!left) {
      left = u16(p + at);
      value = p[at + 2];
      at += 3;
    }
    --left;
    return value;
  }
};
// NMP1 stores 30 independent LZ4 blocks of native column stripes. Validation needs
// no framebuffer: matches can only repeat already validated literal palette indices.
inline bool lz4Raw(const uint8_t *src,size_t n,uint8_t *out,size_t expected){
 size_t at=0,used=0;while(at<n){uint8_t token=src[at++];size_t literals=token>>4;if(literals==15){uint8_t extra;do{if(at>=n)return false;extra=src[at++];literals+=extra;if(literals>expected)return false;}while(extra==255);}
 if(literals>n-at||literals>expected-used)return false;memcpy(out+used,src+at,literals);at+=literals;used+=literals;if(at==n)return used==expected;if(at+2>n)return false;size_t offset=u16(src+at);at+=2;if(!offset||offset>used)return false;size_t count=(token&15)+4;if((token&15)==15){uint8_t extra;do{if(at>=n)return false;extra=src[at++];count+=extra;if(count>expected)return false;}while(extra==255);}if(count>expected-used)return false;for(size_t j=0;j<count;j++)out[used+j]=out[used+j-offset];used+=count;}
 return used==expected;
}
inline bool lz4Stripe(const uint8_t *p,size_t n,uint8_t *out,size_t expected,uint8_t bits,uint8_t paletteSize){
  size_t at=0,used=0;
  while(at<n){
    const uint8_t token=p[at++];size_t literals=token>>4;
    if(literals==15){uint8_t extra;do{if(at>=n)return false;extra=p[at++];literals+=extra;if(literals>expected)return false;}while(extra==255);}
    if(literals>n-at||literals>expected-used)return false;
    for(size_t j=0;j<literals;++j){const uint8_t byte=p[at+j];for(int shift=8-bits;shift>=0;shift-=bits)if(((byte>>shift)&((1<<bits)-1))>=paletteSize)return false;}
    if(out)memcpy(out+used,p+at,literals);at+=literals;used+=literals;
    if(at==n)return used==expected;
    if(at+2>n)return false;const size_t offset=u16(p+at);at+=2;if(!offset||offset>used)return false;
    size_t match=(token&15)+4;
    if((token&15)==15){uint8_t extra;do{if(at>=n)return false;extra=p[at++];match+=extra;if(match>expected)return false;}while(extra==255);}
    if(match>expected-used)return false;
    if(out)for(size_t j=0;j<match;++j)out[used+j]=out[used+j-offset];used+=match;
  }
  return false;
}
inline bool validNative(const uint8_t *p,size_t n){
  if(n<18||n>32768||memcmp(p,"NMP1",4)||u16(p+4)!=480||u16(p+6)!=720||crc32(p+12,n-12)!=u32(p+8))return false;
  const uint8_t bits=p[12],count=p[13];if((bits!=1&&bits!=2&&bits!=4)||count<1||count>6||count>(1<<bits)||14u+count>n)return false;
  for(int i=0;i<count;++i){if(!color(p[14+i]))return false;for(int j=0;j<i;++j)if(p[14+i]==p[14+j])return false;}
  size_t at=14+count;
  for(int band=0;band<30;++band){if(at+2>n)return false;const size_t size=u16(p+at);at+=2;if(!size||size>n-at||!lz4Stripe(p+at,size,nullptr,16*720*bits/8,bits,count))return false;at+=size;}
  return at==n;
}
inline bool validRaster(const uint8_t *p,size_t n){return n>=4&&(!memcmp(p,"NMP1",4)?validNative(p,n):validRle(p,n));}
inline bool nativeStripe(const uint8_t *p,int band,uint8_t *out){
  if(band<0||band>=30)return false;const uint8_t bits=p[12],count=p[13];size_t at=14+count;
  for(int i=0;i<band;++i)at+=2+u16(p+at);const size_t size=u16(p+at);at+=2;
  if(!lz4Stripe(p+at,size,out,16*720*bits/8,bits,count))return false;
  // Expand backwards in the same 5760-byte stripe, keeping the packed input intact.
  const uint8_t mask=(1<<bits)-1;
  for(int pair=5759;pair>=0;--pair){const int a=pair*2,b=a+1;
    const uint8_t left=(out[a*bits/8]>>(8-bits-(a*bits%8)))&mask;
    const uint8_t right=(out[b*bits/8]>>(8-bits-(b*bits%8)))&mask;
    out[pair]=(p[14+left]<<4)|p[14+right];
  }
  return true;
}
inline bool due(uint32_t now, uint32_t deadline) {
  return int32_t(now - deadline) >= 0;
}
} // namespace nm
