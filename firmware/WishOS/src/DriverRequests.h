#pragma once
#include <ArduinoJson.h>
#include <math.h>
#include <initializer_list>
namespace notesos {
struct ClockChange{bool cpu=false,wall=false;uint32_t cpuMhz=0;int64_t unixSeconds=0;};
inline const char*validateClockChange(JsonObjectConst a,ClockChange&c,std::initializer_list<uint32_t> allowed={}){
  for(auto field:a)if(strcmp(field.key().c_str(),"cpu_mhz")&&strcmp(field.key().c_str(),"unix_s"))return "unknown_driver_setting";
  if(a.containsKey("cpu_mhz")){
    if(!a["cpu_mhz"].is<uint32_t>())return "invalid_cpu_clock";
    c.cpu=true;c.cpuMhz=a["cpu_mhz"];bool supported=false;for(auto mhz:allowed)supported|=mhz==c.cpuMhz;if(!supported)return "unsupported_cpu_clock";
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
