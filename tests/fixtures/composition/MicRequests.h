#pragma once
#include <ArduinoJson.h>
#include <string.h>
namespace notesos {
// This composition retains its established microphone transport contract.
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
}
