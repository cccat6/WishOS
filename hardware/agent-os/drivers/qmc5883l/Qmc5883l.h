#pragma once
#include "drivers/i2c/RegisterBus.h"
#include "drivers/sensor-sampling/Sampling.h"
namespace notesos::drivers {
class Qmc5883l final:public SampleBackend {
 I2cBus&bus_;uint8_t address_;unsigned rate_=100,range_=2,oversample_=512;bool configured_=false,pending_=false,enabled_=true;int16_t raw_[3]{};bool overrun_=false;
 static int index(unsigned n,const unsigned*values,size_t size){for(size_t i=0;i<size;++i)if(n==values[i])return i;return -1;}
public:
 explicit Qmc5883l(I2cBus&bus,uint8_t address=0x0d):bus_(bus),address_(address){}
 uint32_t timeoutMs()const override{return 250;}
 void describe(JsonObject o)const override{o["model"]="QMC5883L";o["transport"]="I2C";o["address"]=address_;o["settings"]["enabled"]="boolean";o["settings"]["rate_hz"]="10,50,100,200";o["settings"]["range_gauss"]="2,8";o["settings"]["oversampling"]="512,256,128,64";
  for(auto key:{"x_ut","y_ut","z_ut"}){auto p=o["properties"][key].to<JsonObject>();p["type"]="number";p["unit"]="uT";p["access"]="r";}o["heading"]="application calibration and orientation fusion";}
 const char*configure(JsonObjectConst a,uint64_t)override{
  for(auto f:a)if(strcmp(f.key().c_str(),"rate_hz")&&strcmp(f.key().c_str(),"range_gauss")&&strcmp(f.key().c_str(),"oversampling")&&strcmp(f.key().c_str(),"enabled"))return "unknown_sensor_setting";
  for(auto key:{"rate_hz","range_gauss","oversampling"})if(a.containsKey(key)&&!a[key].is<unsigned>())return "sensor_setting_type";
  if(a.containsKey("enabled")&&!a["enabled"].is<bool>())return "sensor_setting_type";bool enabled=a["enabled"]|enabled_;
  unsigned rate=a["rate_hz"]|rate_,range=a["range_gauss"]|range_,osr=a["oversampling"]|oversample_;const unsigned rates[]={10,50,100,200},ranges[]={2,8},samples[]={512,256,128,64};
  int r=index(rate,rates,4),g=index(range,ranges,2),s=index(osr,samples,4);if(r<0||g<0||s<0)return "sensor_setting_range";
  I2cBus::Transaction t(bus_);if(!t.held)return "bus_busy";configured_=false;
  if(auto e=t.write(address_,0x09,0))return e;if(auto e=t.write(address_,0x0b,1))return e;if(auto e=t.write(address_,0x0a,0))return e;
  if(auto e=t.write(address_,0x09,uint8_t((s<<6)|(g<<4)|(r<<2)|(enabled?1:0))))return e;
  enabled_=enabled;rate_=rate;range_=range;oversample_=osr;configured_=true;return nullptr;
 }
 void configuration(JsonObject o)const override{o["configured"]=configured_;o["enabled"]=enabled_;o["rate_hz"]=rate_;o["range_gauss"]=range_;o["oversampling"]=oversample_;}
 const char*begin(uint64_t)override{if(!configured_)return "sensor_not_configured";if(!enabled_)return "sensor_disabled";pending_=true;return nullptr;}
 SampleStep poll(uint64_t)override{if(!pending_)return {};I2cBus::Transaction t(bus_);if(!t.held)return {};uint8_t status=0,data[6];if(auto e=t.read(address_,6,&status,1))return {false,e};if(!(status&1))return {};
  if(auto e=t.read(address_,0,data,6))return {false,e};pending_=false;if(status&2)return {false,"magnetic_overflow"};for(unsigned i=0;i<3;++i)raw_[i]=signedLE(data+i*2);overrun_=status&4;return {true,nullptr};}
 const char*cancel()override{pending_=false;return nullptr;}
 void values(JsonObject o)const override{float divisor=range_==2?120.0f:30.0f;o["x_ut"]=raw_[0]/divisor;o["y_ut"]=raw_[1]/divisor;o["z_ut"]=raw_[2]/divisor;o["overrun"]=overrun_;}
};
}
