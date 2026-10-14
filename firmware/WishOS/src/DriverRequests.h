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
inline const char*validateMicArguments(JsonObjectConst a){
  for(auto f:a)if(strcmp(f.key().c_str(),"scope")&&strcmp(f.key().c_str(),"sample_rate")&&strcmp(f.key().c_str(),"max_ms")&&strcmp(f.key().c_str(),"storage")&&strcmp(f.key().c_str(),"network_queue")&&strcmp(f.key().c_str(),"expected_boot_id"))return "unknown_mic_argument";
  if(a.containsKey("scope")&&(!a["scope"].is<const char*>()||!strlen(a["scope"].as<const char*>())||strlen(a["scope"].as<const char*>())>48||strchr(a["scope"].as<const char*>(),'/')))return "mic_scope";
  if(a.containsKey("sample_rate")&&!a["sample_rate"].is<uint32_t>())return "mic_rate_type";
  if(a.containsKey("max_ms")&&!a["max_ms"].isNull()&&(!a["max_ms"].is<uint32_t>()||!a["max_ms"].as<uint32_t>()))return "mic_duration";
  if(a.containsKey("network_queue")&&!a["network_queue"].is<bool>())return "mic_sink_type";
  if(a.containsKey("storage")){if(!a["storage"].is<const char*>())return "mic_storage_type";const char*s=a["storage"];if(strcmp(s,"off")&&strcmp(s,"retry")&&strcmp(s,"history"))return "unsupported_audio_storage";}
  return nullptr;
}
inline const char*validateMicControl(const char*method,JsonObjectConst a,const char*recording){
 for(auto f:a)if(strcmp(f.key().c_str(),"recording_id")&&(strcmp(method,"read_chunk")||strcmp(f.key().c_str(),"limit")))return "unknown_mic_argument";
 if(a.containsKey("recording_id")){
  if(!a["recording_id"].is<const char*>()||strlen(a["recording_id"].as<const char*>())!=a["recording_id"].as<JsonString>().size())return "recording_id_type";
  if(strcmp(a["recording_id"],recording))return "recording_replaced";
 }
 if(!strcmp(method,"read_chunk")&&a.containsKey("limit")&&(!a["limit"].is<uint32_t>()||!a["limit"].as<uint32_t>()||a["limit"].as<uint32_t>()>16))return "audio_read_limit";
 return nullptr;
}
inline bool httpStatusSuccessful(int status){return status>=200&&status<300;}
struct HttpOutcome{bool transportOk,httpOk,ok;const char*error;};
inline HttpOutcome httpOutcome(int status,const char*transportError){
  bool transport=status>0&&(!transportError||!*transportError),http=httpStatusSuccessful(status);
  return {transport,http,transport&&http,transportError&&*transportError?transportError:!transport?"network_request_failed":!http?"http_status_failed":""};
}
}
