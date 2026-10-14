#pragma once
#include <stdint.h>
#include <stddef.h>
namespace notesos {
// Independently decodable IMA ADPCM packets; low nibble precedes high nibble.
constexpr int16_t IMA_STEP[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
constexpr int8_t IMA_INDEX[8]={-1,-1,-1,-1,2,4,6,8};
struct VoiceCodec {
  int32_t predictor=0;int index=0;
  int16_t decode(uint8_t code){int difference=(IMA_STEP[index]*(2*(code&7)+1))>>3;predictor+=(code&8)?-difference:difference;if(predictor>32767)predictor=32767;if(predictor<-32768)predictor=-32768;index+=IMA_INDEX[code&7];if(index<0)index=0;if(index>88)index=88;return int16_t(predictor);}
  uint8_t encode(int16_t sample){
    int step=IMA_STEP[index],delta=int(sample)-predictor;uint8_t code=0;
    if(delta<0){code=8;delta=-delta;}
    int difference=step>>3;
    if(delta>=step){code|=4;delta-=step;difference+=step;}
    if(delta>=(step>>1)){code|=2;delta-=step>>1;difference+=step>>1;}
    if(delta>=(step>>2)){code|=1;difference+=step>>2;}
    difference=(step*(2*(code&7)+1))>>3;
    predictor+=(code&8)?-difference:difference;
    if(predictor>32767)predictor=32767;if(predictor<-32768)predictor=-32768;
    index+=IMA_INDEX[code&7];if(index<0)index=0;if(index>88)index=88;
    return code;
  }
};
inline bool decodeVoiceBlock(const uint8_t*bytes,size_t n,int16_t*out,size_t capacity,size_t&samples){
 samples=0;if(n<11||bytes[0]!=0x42||bytes[7]>88||bytes[8])return false;size_t count=size_t(bytes[9])+(size_t(bytes[10])<<8);if(!count||count>capacity||n!=11+(count+1)/2)return false;
 VoiceCodec codec;codec.predictor=int16_t(uint16_t(bytes[5])|(uint16_t(bytes[6])<<8));codec.index=bytes[7];for(size_t i=0;i<count;++i)out[i]=codec.decode((bytes[11+i/2]>>((i&1)*4))&15);samples=count;return true;
}
}
