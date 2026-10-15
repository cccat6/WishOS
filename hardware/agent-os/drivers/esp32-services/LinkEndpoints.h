#pragma once
#include "Core.h"
namespace notesos {
// Configuration belongs to the link adapter, independent of Harness/vendor/UI.
struct LinkEndpoints {
 using Validate=const char*(*)(JsonObjectConst);
 static unsigned count(JsonObjectConst c){return c["enabled"]==true?(c["endpoints"].is<JsonArrayConst>()?c["endpoints"].size():1):0;}
 static void endpoint(JsonObjectConst c,unsigned index,JsonObject o){if(c["endpoints"].is<JsonArrayConst>()){o.set(c["endpoints"][index]);o.remove("id");}else{o.set(c);o.remove("enabled");}}
 static const char*id(JsonObjectConst c,unsigned index){return c["endpoints"].is<JsonArrayConst>()?c["endpoints"][index]["id"]|"":"default";}
 static const char*validate(JsonObjectConst c,Validate transport){
  if(!c["enabled"].is<bool>())return "link_enabled_required";
  if(c["enabled"]==false)return c.size()==1?nullptr:"disabled_link_settings";
  if(measureJson(c)>MAX_WIRE-1024)return "link_configuration_limit";
  if(!c.containsKey("endpoints")){if(auto e=argumentFields(c,"enabled,host,path,ca_pem,tls,verify_peer,port,reconnect_ms,headers,ping_interval_ms,pong_timeout_ms,pong_misses"))return e;JsonDocument a;endpoint(c,0,a.to<JsonObject>());return transport(a.as<JsonObjectConst>());}
  if(auto e=argumentFields(c,"enabled,endpoints,attempt_timeout_ms,retry_ms,probe_interval_ms,idle_ms"))return e;
  auto endpoints=c["endpoints"].as<JsonArrayConst>();if(!endpoints.size()||endpoints.size()>4)return "link_endpoint_count";
  for(auto key:{"attempt_timeout_ms","retry_ms","probe_interval_ms","idle_ms"})if(c.containsKey(key)&&!c[key].is<uint32_t>())return "link_policy_type";
  uint32_t attempt=c["attempt_timeout_ms"]|20000u,retry=c["retry_ms"]|5000u,probe=c["probe_interval_ms"]|60000u,idle=c["idle_ms"]|250u;
  if(attempt<1000||attempt>120000||retry<100||retry>3600000||probe&&probe<1000||probe>86400000||idle>60000)return "link_policy_range";
  size_t largest=0;unsigned index=0;for(auto e:endpoints){if(!e.is<JsonObjectConst>()||!e["id"].is<const char*>()||!strlen(e["id"])||strlen(e["id"])>48)return "link_endpoint_id";for(unsigned earlier=0;earlier<index;++earlier)if(endpoints[earlier]["id"]==e["id"])return "link_endpoint_duplicate";JsonDocument a;endpoint(c,index++,a.to<JsonObject>());if(auto error=transport(a.as<JsonObjectConst>()))return error;for(auto key:{"headers","ca_pem","reconnect_ms"})a.remove(key);largest=std::max(largest,measureJson(a));}
  if(measureJson(c)+largest+1024>MAX_WIRE)return "link_configuration_limit";return nullptr;
 }
};
}
