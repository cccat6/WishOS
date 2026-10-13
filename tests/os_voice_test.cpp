#include "VoiceCodec.h"
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
using namespace notesos;
int main(){VoiceCodec encoder;std::ofstream packets("build/os-voice-packets.bin",std::ios::binary);double error=0,signal=0;int32_t predict=0;int index=0;
 for(unsigned seq=0;seq<100;++seq){uint8_t packet[91]{};packet[0]=0x42;for(int i=0;i<4;++i)packet[1+i]=seq>>(8*i);packet[5]=encoder.predictor;packet[6]=uint16_t(encoder.predictor)>>8;packet[7]=encoder.index;packet[9]=160;
   predict=int16_t(packet[5]|uint16_t(packet[6])<<8);index=packet[7];
   for(unsigned i=0;i<160;++i){int16_t sample=12000*sin((seq*160+i)*2*3.14159265*440/16000);uint8_t code=encoder.encode(sample);packet[11+i/2]|=code<<((i&1)*4);int step=IMA_STEP[index],delta=(step*(2*(code&7)+1))>>3;predict+=(code&8)?-delta:delta;predict=std::max(-32768,std::min(32767,predict));index=std::max(0,std::min(88,index+IMA_INDEX[code&7]));if(seq>2){error+=double(sample-predict)*(sample-predict);signal+=double(sample)*sample;}}
   assert(predict==encoder.predictor&&index==encoder.index);packets.write((char*)packet,sizeof packet);
 }assert(signal/error>100);std::cout<<"IMA ADPCM independent packet decode, 320 to 91 bytes per block and voice reconstruction passed\n";}
