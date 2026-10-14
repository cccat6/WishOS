#pragma once
#include "drivers/i2c/RegisterBus.h"
#include "drivers/sensor-sampling/Sampling.h"
#include <cmath>
namespace notesos::drivers {
class Qmi8658 final:public SampleBackend {
public:
 // QST revisions differ in gyro range and nominal ODR; the native device
 // description chooses the documented silicon profile, never the MCU model.
 enum class Profile{Rev09,RevA};
private:
 I2cBus&bus_;uint8_t address_;Profile profile_;bool configured_=false,pending_=false,accel_=true,gyro_=true;
 unsigned accRange_=4,gyroRange_=512;double rate_;uint64_t readyAt_=0;uint32_t settleMs_=200;int16_t acc_[3]{},gyr_[3]{},temperature_=0;uint32_t ticks_=0;
 double nominal(unsigned code,bool gyro)const{static const double old[]={8000,4000,2000,1000,500,250,125,62.5,31.25};static const double revA[]={7174.4,3587.2,1793.6,896.8,448.4,224.2,112.1,56.05,28.025};if(code<9)return gyro&&profile_==Profile::RevA?revA[code]:old[code];static const double low[]={128,21,11,3};return code>=12&&code<=15?low[code-12]:0;}
 int rateCode(double rate,bool gyro)const{for(unsigned c=0;c<16;++c)if(nominal(c,gyro)==rate&&(!gyro||c<9)&&(gyro||c>=3))return c;return -1;}
 static int powerIndex(unsigned value,unsigned base,unsigned count){for(unsigned i=0;i<count;++i)if(value==(base<<i))return i;return -1;}
public:
 explicit Qmi8658(I2cBus&b,Profile profile,uint8_t address=0x6a):bus_(b),address_(address),profile_(profile),rate_(profile==Profile::RevA?112.1:125){}
 uint32_t timeoutMs()const override{return settleMs_+2000;}
 void describe(JsonObject o)const override{o["model"]="QMI8658C";o["silicon_profile"]=profile_==Profile::RevA?"QST Rev A":"QST Rev 0.9";o["transport"]="I2C";o["address"]=address_;o["self_test_during_measurement"]=false;
  o["settings"]["accel_enabled"]="boolean";o["settings"]["gyro_enabled"]="boolean";auto a=o["settings"]["accel_range_g"].to<JsonArray>();for(auto v:{2,4,8,16})a.add(v);
  auto g=o["settings"]["gyro_range_dps"].to<JsonArray>();unsigned base=profile_==Profile::RevA?16:32;for(unsigned i=0;i<(profile_==Profile::RevA?7u:8u);++i)g.add(base<<i);
  auto rates=o["settings"]["odr_hz"]["choices_with_gyro"].to<JsonArray>();for(unsigned i=0;i<9;++i)rates.add(nominal(i,true));auto low=o["settings"]["odr_hz"]["choices_accel_only"].to<JsonArray>();for(unsigned i=3;i<16;++i)if(nominal(i,false)>0)low.add(nominal(i,false));
  for(auto key:{"accel_x_g","accel_y_g","accel_z_g","gyro_x_dps","gyro_y_dps","gyro_z_dps","die_temperature_c"}){auto p=o["properties"][key].to<JsonObject>();p["type"]="number";p["access"]="r";p["unit"]=!strncmp(key,"accel",5)?"g":!strncmp(key,"gyro",4)?"deg/s":"degC";}}
 const char*configure(JsonObjectConst a,uint64_t now)override{
  for(auto f:a)if(strcmp(f.key().c_str(),"accel_enabled")&&strcmp(f.key().c_str(),"gyro_enabled")&&strcmp(f.key().c_str(),"accel_range_g")&&strcmp(f.key().c_str(),"gyro_range_dps")&&strcmp(f.key().c_str(),"odr_hz"))return "unknown_sensor_setting";
  for(auto key:{"accel_enabled","gyro_enabled"})if(a.containsKey(key)&&!a[key].is<bool>())return "sensor_setting_type";
  for(auto key:{"accel_range_g","gyro_range_dps"})if(a.containsKey(key)&&!a[key].is<unsigned>())return "sensor_setting_type";
  if(a.containsKey("odr_hz")&&(!a["odr_hz"].is<double>()||!std::isfinite(a["odr_hz"].as<double>())))return "sensor_setting_type";
  bool acc=a["accel_enabled"]|accel_,gyro=a["gyro_enabled"]|gyro_;unsigned ar=a["accel_range_g"]|accRange_,gr=a["gyro_range_dps"]|gyroRange_;
  double hz=a["odr_hz"]|rate_;int ai=powerIndex(ar,2,4),gi=powerIndex(gr,profile_==Profile::RevA?16:32,profile_==Profile::RevA?7:8),odr=rateCode(hz,gyro);
  if(ai<0||gi<0||odr<0)return "sensor_setting_range";
  I2cBus::Transaction t(bus_);if(!t.held)return "bus_busy";uint8_t id=0;if(auto e=t.read(address_,0,&id,1))return e;if(id!=5)return "sensor_identity";configured_=false;
  if(auto e=t.write(address_,8,0))return e;if(auto e=t.write(address_,2,0x40))return e;
  // Normal measurements must not set CTRL2.aST or CTRL3.gST (bit 7).
  if(auto e=t.write(address_,3,uint8_t((ai<<4)|odr)))return e;if(gyro)if(auto e=t.write(address_,4,uint8_t((gi<<4)|odr)))return e;
  if(auto e=t.write(address_,6,0))return e;if(auto e=t.write(address_,8,uint8_t(acc|(gyro<<1))))return e;
  accel_=acc;gyro_=gyro;accRange_=ar;gyroRange_=gr;rate_=hz;settleMs_=uint32_t((gyro?150:3)+std::ceil(3000.0/hz));readyAt_=bus_.after(now)+settleMs_+1;configured_=true;return nullptr;
 }
 const char*begin(uint64_t)override{if(!configured_)return "sensor_not_configured";if(!accel_&&!gyro_)return "sensor_disabled";pending_=true;return nullptr;}
 SampleStep poll(uint64_t now)override{if(!pending_||now<readyAt_)return {};I2cBus::Transaction t(bus_);if(!t.held)return {};uint8_t status=0,data[17];if(auto e=t.read(address_,0x2e,&status,1))return {false,e};uint8_t required=uint8_t(accel_|(gyro_<<1));if((status&required)!=required)return {};
  if(auto e=t.read(address_,0x30,data,sizeof data))return {false,e};ticks_=uint32_t(data[0])|uint32_t(data[1])<<8|uint32_t(data[2])<<16;temperature_=signedLE(data+3);for(unsigned i=0;i<3;++i){acc_[i]=signedLE(data+5+i*2);gyr_[i]=signedLE(data+11+i*2);}pending_=false;return {true,nullptr};}
 const char*cancel()override{pending_=false;return nullptr;}
 void values(JsonObject o)const override{if(accel_){o["accel_x_g"]=double(acc_[0])*accRange_/32768;o["accel_y_g"]=double(acc_[1])*accRange_/32768;o["accel_z_g"]=double(acc_[2])*accRange_/32768;}if(gyro_){o["gyro_x_dps"]=double(gyr_[0])*gyroRange_/32768;o["gyro_y_dps"]=double(gyr_[1])*gyroRange_/32768;o["gyro_z_dps"]=double(gyr_[2])*gyroRange_/32768;}o["die_temperature_c"]=double(temperature_)/256;o["sensor_timestamp_ticks"]=ticks_;o["nominal_odr_hz"]=rate_;}
};
}
