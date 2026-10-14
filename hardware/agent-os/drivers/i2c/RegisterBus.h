#pragma once
#include <stdint.h>
#include <stddef.h>
namespace notesos::drivers {
// Native-only transport. Every transaction, including status/identity reads,
// requires the device composition's shared bus arbiter.
struct I2cBus {
 void*context;
 bool(*acquire)(void*);
 void(*release)(void*);
 const char*(*transfer)(void*,uint8_t,const uint8_t*,size_t,uint8_t*,size_t);
 uint64_t(*monotonicMs)(void*)=nullptr;
 uint64_t after(uint64_t started)const{return monotonicMs?monotonicMs(context):started;}
 struct Transaction {
  I2cBus&bus;bool held;
  explicit Transaction(I2cBus&b):bus(b),held(b.acquire(b.context)){}
  ~Transaction(){if(held)bus.release(bus.context);}
  Transaction(const Transaction&)=delete;Transaction&operator=(const Transaction&)=delete;
  const char*io(uint8_t address,const uint8_t*tx,size_t tn,uint8_t*rx,size_t rn){return held?bus.transfer(bus.context,address,tx,tn,rx,rn):"bus_busy";}
  const char*read(uint8_t a,uint8_t reg,uint8_t*out,size_t n){return io(a,&reg,1,out,n);}
  const char*write(uint8_t a,uint8_t reg,uint8_t value){uint8_t bytes[]={reg,value};return io(a,bytes,2,nullptr,0);}
  const char*command(uint8_t a,uint16_t command){uint8_t bytes[]={uint8_t(command>>8),uint8_t(command)};return io(a,bytes,2,nullptr,0);}
 };
};
inline int16_t signedLE(const uint8_t*p){return int16_t(uint16_t(p[0])|uint16_t(p[1])<<8);}
inline uint8_t crc8(const uint8_t*p,size_t n){uint8_t crc=255;while(n--){crc^=*p++;for(unsigned bit=0;bit<8;++bit)crc=uint8_t((crc<<1)^((crc&128)?0x31:0));}return crc;}
}
