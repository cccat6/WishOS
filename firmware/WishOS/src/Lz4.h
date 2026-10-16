#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace nm {
// Bounded single-pass encoder for small native metadata. No heap or media I/O.
inline size_t lz4Bound(size_t n){return n+n/255+16;}
inline size_t lz4Pack(const uint8_t*src,size_t n,uint8_t*out,size_t capacity){
 if(n>65535||capacity<lz4Bound(n))return 0;
 uint16_t table[256]{};size_t at=0,anchor=0,used=0;
 auto length=[&](size_t value){while(value>=255){out[used++]=255;value-=255;}out[used++]=uint8_t(value);};
 while(at+12<n){uint32_t word;memcpy(&word,src+at,4);unsigned hash=(word*2654435761u)>>24;unsigned prior=table[hash];table[hash]=uint16_t(at+1);
  if(!prior||memcmp(src+prior-1,src+at,4)){++at;continue;}size_t previous=prior-1,matched=4;
  while(at+matched<n-5&&src[previous+matched]==src[at+matched])++matched;
  size_t literals=at-anchor;out[used++]=uint8_t((literals<15?literals:15)<<4)|uint8_t(matched<19?matched-4:15);
  if(literals>=15)length(literals-15);if(literals)memcpy(out+used,src+anchor,literals);used+=literals;size_t offset=at-previous;out[used++]=uint8_t(offset);out[used++]=uint8_t(offset>>8);
  if(matched>=19)length(matched-19);at+=matched;anchor=at;
 }
 size_t literals=n-anchor;out[used++]=uint8_t((literals<15?literals:15)<<4);if(literals>=15)length(literals-15);if(literals)memcpy(out+used,src+anchor,literals);return used+literals;
}
inline bool lz4Raw(const uint8_t *src,size_t n,uint8_t *out,size_t expected){
 size_t at=0,used=0;while(at<n){uint8_t token=src[at++];size_t literals=token>>4;if(literals==15){uint8_t extra;do{if(at>=n)return false;extra=src[at++];literals+=extra;if(literals>expected)return false;}while(extra==255);}
 if(literals>n-at||literals>expected-used)return false;memcpy(out+used,src+at,literals);at+=literals;used+=literals;if(at==n)return used==expected;if(at+2>n)return false;size_t offset=src[at]|size_t(src[at+1])<<8;at+=2;if(!offset||offset>used)return false;size_t count=(token&15)+4;if((token&15)==15){uint8_t extra;do{if(at>=n)return false;extra=src[at++];count+=extra;if(count>expected)return false;}while(extra==255);}if(count>expected-used)return false;for(size_t j=0;j<count;j++)out[used+j]=out[used+j-offset];used+=count;}
 return used==expected;
}
}
