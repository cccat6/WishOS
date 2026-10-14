#pragma once
#include "drivers/i2c/RegisterBus.h"
#include <Wire.h>
#include <freertos/semphr.h>
#include <esp_timer.h>
namespace notesos::drivers {
// Wire initialization and signal assignment belong to the native composition.
// All clients of that physical controller must share this same semaphore.
class WireI2c {
 TwoWire&wire_;SemaphoreHandle_t mutex_;uint32_t hz_;uint16_t timeout_;
 static bool acquire(void*p){auto&s=*static_cast<WireI2c*>(p);return xSemaphoreTake(s.mutex_,0)==pdTRUE;}
 static void release(void*p){xSemaphoreGive(static_cast<WireI2c*>(p)->mutex_);}
 static uint64_t clock(void*){return uint64_t(esp_timer_get_time())/1000;}
 static const char*transfer(void*p,uint8_t address,const uint8_t*tx,size_t tn,uint8_t*rx,size_t rn){auto&s=*static_cast<WireI2c*>(p);s.wire_.setClock(s.hz_);s.wire_.setTimeOut(s.timeout_);
  if(tn){s.wire_.beginTransmission(address);if(s.wire_.write(tx,tn)!=tn){s.wire_.endTransmission(true);return "i2c_write_limit";}if(s.wire_.endTransmission(rn==0))return "i2c_nack";}
  if(rn){if(s.wire_.requestFrom(address,rn,true)!=rn)return "i2c_short_read";for(size_t i=0;i<rn;++i)rx[i]=s.wire_.read();}return nullptr;
 }
public:
 I2cBus bus;
 WireI2c(TwoWire&wire,SemaphoreHandle_t mutex,uint32_t hz,uint16_t timeoutMs=10):wire_(wire),mutex_(mutex),hz_(hz),timeout_(timeoutMs),bus{this,acquire,release,transfer,clock}{}
};
}
