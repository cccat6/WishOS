#pragma once
#include <ArduinoJson.h>
#include <math.h>
namespace notesos {
struct WifiChange{bool power=false,save=false;int8_t quarterDbm=0;int saveMode=0;};
inline const char*validateWifiChange(JsonObjectConst a,WifiChange&c){
  for(auto field:a)if(strcmp(field.key().c_str(),"tx_power_dbm")&&strcmp(field.key().c_str(),"power_save"))return "unknown_driver_setting";
  if(a.containsKey("tx_power_dbm")){
    if(!a["tx_power_dbm"].is<double>())return "wifi_power_type";
    double v=a["tx_power_dbm"].as<double>();if(!isfinite(v)||v<2||v>20)return "wifi_power_range";
    c.power=true;c.quarterDbm=int8_t(lround(v*4));
  }
  if(a.containsKey("power_save")){
    if(!a["power_save"].is<const char*>())return "invalid_power_save";
    const char*s=a["power_save"];c.save=true;
    if(!strcmp(s,"off"))c.saveMode=0;else if(!strcmp(s,"minimum"))c.saveMode=1;
    else if(!strcmp(s,"maximum"))c.saveMode=2;else return "invalid_power_save";
  }
  return nullptr;
}
struct ClockChange{bool cpu=false,wall=false;uint32_t cpuMhz=0;int64_t unixSeconds=0;};
inline const char*validateClockChange(JsonObjectConst a,ClockChange&c){
  for(auto field:a)if(strcmp(field.key().c_str(),"cpu_mhz")&&strcmp(field.key().c_str(),"unix_s"))return "unknown_driver_setting";
  if(a.containsKey("cpu_mhz")){
    if(!a["cpu_mhz"].is<uint32_t>())return "invalid_cpu_clock";
    c.cpu=true;c.cpuMhz=a["cpu_mhz"];if(c.cpuMhz!=80&&c.cpuMhz!=160)return "cpu_mhz_80_or_160_required";
  }
  if(a.containsKey("unix_s")){
    if(!a["unix_s"].is<int64_t>()||a["unix_s"].as<int64_t>()<0)return "invalid_unix_time";
    c.wall=true;c.unixSeconds=a["unix_s"];
  }
  return nullptr;
}
inline bool httpStatusSuccessful(int status){return status>=200&&status<300;}
struct HttpOutcome{bool transportOk,httpOk,ok;const char*error;};
inline HttpOutcome httpOutcome(int status,const char*transportError){
  bool transport=status>0&&(!transportError||!*transportError),http=httpStatusSuccessful(status);
  return {transport,http,transport&&http,transportError&&*transportError?transportError:!transport?"network_request_failed":!http?"http_status_failed":""};
}
}
