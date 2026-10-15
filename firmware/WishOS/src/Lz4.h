#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace nm {
inline bool lz4Raw(const uint8_t *src,size_t n,uint8_t *out,size_t expected){
 size_t at=0,used=0;while(at<n){uint8_t token=src[at++];size_t literals=token>>4;if(literals==15){uint8_t extra;do{if(at>=n)return false;extra=src[at++];literals+=extra;if(literals>expected)return false;}while(extra==255);}
 if(literals>n-at||literals>expected-used)return false;memcpy(out+used,src+at,literals);at+=literals;used+=literals;if(at==n)return used==expected;if(at+2>n)return false;size_t offset=src[at]|size_t(src[at+1])<<8;at+=2;if(!offset||offset>used)return false;size_t count=(token&15)+4;if((token&15)==15){uint8_t extra;do{if(at>=n)return false;extra=src[at++];count+=extra;if(count>expected)return false;}while(extra==255);}if(count>expected-used)return false;for(size_t j=0;j<count;j++)out[used+j]=out[used+j-offset];used+=count;}
 return used==expected;
}
}
