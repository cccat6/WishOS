#pragma once
#include "Core.h"
#include "Policy.h"
namespace notesos {
// Endpoints are app configuration. Saving them does not claim a transport handshake succeeded.
class ConnectionProfiles {
 JsonDocument value_;uint32_t revision_=1;bool saved_=true;
 static const char*validate(JsonObjectConst o){for(auto f:o){if(!Policies::choice("mcp,agent,llm",f.key().c_str())||!f.value().is<JsonObjectConst>())return "connection_role";auto p=f.value().as<JsonObjectConst>();for(auto k:p)if(!Policies::choice("url,token,model,enabled",k.key().c_str()))return "connection_field";const char*url=p["url"]|"";if(strlen(url)>512||(*url&&strncmp(url,"http://",7)&&strncmp(url,"https://",8)&&strncmp(url,"ws://",5)&&strncmp(url,"wss://",6)))return "connection_url";if(p.containsKey("enabled")&&!p["enabled"].is<bool>())return "connection_enabled";for(auto key:{"token","model","url"})if(p.containsKey(key)&&!p[key].is<const char*>())return "connection_type";if(strlen(p["token"]|"")>1024||strlen(p["model"]|"")>128)return "connection_limit";}return nullptr;}
public:
 using Save=bool(*)(const char*,size_t);
 bool restore(const char*text){JsonDocument d;if(deserializeJson(d,text)||!d.is<JsonObject>()||validate(d.as<JsonObjectConst>()))return false;swap(value_,d);return true;}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out,Save save){
  if(!strcmp(m,"configure")){for(auto field:a)if(!Policies::choice("profiles,persist,expected_revision",field.key().c_str()))return "unknown_connection_setting";if(a.containsKey("persist")&&!a["persist"].is<bool>())return "connection_type";if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";if(!a["profiles"].is<JsonObjectConst>())return "profiles_required";JsonDocument next;next.set(value_);for(auto p:a["profiles"].as<JsonObjectConst>())next[p.key()]=p.value();if(next.overflowed())return "memory_low";if(auto e=validate(next.as<JsonObjectConst>()))return e;size_t n=measureJson(next);if(n>3072)return "connection_limit";bool persist=a["persist"]|false;if(persist){char*text=(char*)malloc(n+1);if(!text)return "memory_low";serializeJson(next,text,n+1);bool ok=save&&save(text,n);free(text);if(!ok)return "persistence_failed";}swap(value_,next);saved_=persist;++revision_;}
  else if(strcmp(m,"read"))return "unsupported_method";
  out["profiles"]=value_;out["revision"]=revision_;out["persisted"]=saved_;out["state"]="configured";return nullptr;
 }
};
}
