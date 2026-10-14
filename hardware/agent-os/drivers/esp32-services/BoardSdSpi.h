#pragma once
#include <SPI.h>
#include <SdFat.h>

namespace notesos {
// Board adapter: SdFat owns card commands, while this driver owns only SPI
// transactions. Clock changes therefore do not reset the card or its mode.
class BoardSdSpi final:public SdSpiBaseClass {
  SPIClass &bus_;
  uint32_t clockHz_=400000;
public:
  explicit BoardSdSpi(SPIClass &bus):bus_(bus){}
  void begin(SdSpiConfig) override {}
  void end() override {} // The shared bus is owned by the board SPI scheduler.
  void activate() override {bus_.beginTransaction(SPISettings(clockHz_,MSBFIRST,SPI_MODE0));}
  void deactivate() override {bus_.endTransaction();}
  void setSckSpeed(uint32_t hz) override {clockHz_=hz;}
  uint32_t clockHz()const{return clockHz_;}
  uint8_t receive() override{return bus_.transfer(0xff);}
  uint8_t receive(uint8_t*data,size_t length) override{
    uint8_t dummy[64];memset(dummy,0xff,sizeof(dummy));
    while(length){size_t n=length<sizeof(dummy)?length:sizeof(dummy);
      bus_.transferBytes(dummy,data,n);data+=n;length-=n;}
    return 0;
  }
  void send(uint8_t value) override{bus_.transfer(value);}
  void send(const uint8_t*data,size_t length) override{bus_.writeBytes(data,length);}
};
} // namespace notesos
