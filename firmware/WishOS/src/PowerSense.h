#pragma once
#include <stdint.h>
namespace notesos {
struct PowerSample {uint32_t adcMv=0,batteryMv=0;bool batteryValid=false;int externalPower=-1;const char*reason="invalid_adc";};
inline PowerSample decodePowerSample(uint32_t adcMv,bool sharedWith5V,float gain=1.0f,float divider=2.0f){
  PowerSample s;s.adcMv=adcMv;
  if(sharedWith5V&&adcMv<=100){s.externalPower=1;s.reason="battery_adc_clamped_by_external_power";return s;}
  float voltage=adcMv*divider*gain;
  if(voltage>=2000&&voltage<=4600){s.batteryValid=true;s.batteryMv=uint32_t(voltage+0.5f);s.externalPower=sharedWith5V?0:-1;s.reason="measured";}
  else s.reason=sharedWith5V?"power_transition_or_sensor_fault":"battery_adc_invalid";
  return s;
}
}
