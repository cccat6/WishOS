#pragma once
#include "drivers/i2c/RegisterBus.h"
#include "drivers/sensor-sampling/Sampling.h"
namespace notesos::drivers {
class Gxhtc3 final:public SampleBackend {
 I2cBus&bus_;uint8_t address_;bool lowPower_=false;enum class State{Idle,Waking,Converting}state_=State::Idle;
 uint64_t ready_=0;float temperature_=0,humidity_=0;
public:
 explicit Gxhtc3(I2cBus&bus,uint8_t address=0x70):bus_(bus),address_(address){}
 uint32_t timeoutMs()const override{return 100;}
 void describe(JsonObject o)const override{o["model"]="GXHTC3";o["transport"]="I2C";o["address"]=address_;o["settings"]["low_power"]["type"]="boolean";
  auto t=o["properties"]["temperature_c"].to<JsonObject>();t["type"]="number";t["unit"]="degC";t["access"]="r";
  auto h=o["properties"]["relative_humidity_pct"].to<JsonObject>();h["type"]="number";h["unit"]="%RH";h["access"]="r";
  o["clock_stretching"]=false;o["sleep_between_measurements"]=true;o["crc"]="CRC-8 polynomial 0x31 init 0xff";}
 const char*configure(JsonObjectConst a,uint64_t)override{for(auto f:a)if(strcmp(f.key().c_str(),"low_power"))return "unknown_sensor_setting";if(a.containsKey("low_power")&&!a["low_power"].is<bool>())return "sensor_setting_type";lowPower_=a["low_power"]|lowPower_;return nullptr;}
 void configuration(JsonObject o)const override{o["low_power"]=lowPower_;}
 const char*begin(uint64_t now)override{I2cBus::Transaction t(bus_);if(auto e=t.command(address_,0x3517))return e;state_=State::Waking;ready_=bus_.after(now)+2;return nullptr;}
 SampleStep poll(uint64_t now)override{
  if(state_==State::Idle||now<ready_)return {};I2cBus::Transaction t(bus_);if(!t.held)return {};
  if(state_==State::Waking){if(auto e=t.command(address_,lowPower_?0x609c:0x7866))return {false,e};state_=State::Converting;ready_=bus_.after(now)+(lowPower_?3:16);return {};}
  uint8_t data[6];if(auto e=t.io(address_,nullptr,0,data,6))return {false,e};const char*error=(crc8(data,2)!=data[2]||crc8(data+3,2)!=data[5])?"sensor_crc":nullptr;
  if(auto e=t.command(address_,0xb098))return {false,e};state_=State::Idle;if(error)return {false,error};
  temperature_=-45.0f+175.0f*(uint16_t(data[0])<<8|data[1])/65536.0f;humidity_=100.0f*(uint16_t(data[3])<<8|data[4])/65536.0f;return {true,nullptr};
 }
 const char*cancel()override{if(state_==State::Idle)return nullptr;I2cBus::Transaction t(bus_);if(auto e=t.command(address_,0xb098))return e;state_=State::Idle;return nullptr;}
 void values(JsonObject o)const override{o["temperature_c"]=temperature_;o["relative_humidity_pct"]=humidity_;}
};
}
