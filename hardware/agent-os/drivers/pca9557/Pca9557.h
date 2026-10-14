#pragma once
#include "drivers/i2c/RegisterBus.h"
namespace notesos::drivers {
// Private native dependency for LCD CS, amplifier enable and camera power-down.
// It is not registered as an unrestricted GPIO interface for runtime apps.
class Pca9557 {
 I2cBus&bus_;uint8_t address_;
public:
 explicit Pca9557(I2cBus&bus,uint8_t address=0x19):bus_(bus),address_(address){}
 const char*inputs(uint8_t&value){I2cBus::Transaction t(bus_);return t.read(address_,0,&value,1);}
 const char*write(uint8_t mask,uint8_t values){if(values&~mask)return "expander_mask";I2cBus::Transaction t(bus_);uint8_t previous;if(auto e=t.read(address_,1,&previous,1))return e;return t.write(address_,1,uint8_t((previous&~mask)|(values&mask)));}
 const char*configure(uint8_t mask,uint8_t outputs,uint8_t initial){if((outputs|initial)&~mask)return "expander_mask";I2cBus::Transaction t(bus_);uint8_t oldValue,oldDirection;if(auto e=t.read(address_,1,&oldValue,1))return e;if(auto e=t.read(address_,3,&oldDirection,1))return e;
  // Set the latch before enabling outputs; preserve unrelated control lines.
  if(auto e=t.write(address_,1,uint8_t((oldValue&~mask)|(initial&mask))))return e;return t.write(address_,3,uint8_t((oldDirection|mask)&~outputs));}
};
}
