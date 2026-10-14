#pragma once
#include "Core.h"
#include <ctype.h>
#include <strings.h>
namespace notesos {
inline bool validBleUuid(const char*s){size_t n=strlen(s);if(n!=4&&n!=8&&n!=36)return false;for(size_t i=0;i<n;++i)if(n==36&&(i==8||i==13||i==18||i==23)){if(s[i]!='-')return false;}else if(!isxdigit((unsigned char)s[i]))return false;return true;}
inline const char*normalizeBleProfile(JsonObjectConst in,JsonObject out){
 for(auto f:in)if(strcmp(f.key().c_str(),"name")&&strcmp(f.key().c_str(),"mode")&&strcmp(f.key().c_str(),"report_map_hex")&&strcmp(f.key().c_str(),"service_uuid")&&strcmp(f.key().c_str(),"characteristics"))return "unknown_ble_setting";
 for(auto k:{"name","mode","report_map_hex","service_uuid"})if(in.containsKey(k)&&(!in[k].is<const char*>()||in[k].as<JsonString>().size()!=strlen(in[k].as<const char*>())))return "ble_profile_type";
 const char*name=in["name"]|"Agentic Notes",*mode=in["mode"]|"central";
 if(!*name||strlen(name)>29)return "ble_name_limit";
 if(strcmp(mode,"gatt")&&strcmp(mode,"central")&&strcmp(mode,"hid"))return "ble_mode";
 if(strcmp(mode,"hid")&&in.containsKey("report_map_hex"))return "ble_setting_not_supported_in_mode";
 if(strcmp(mode,"gatt")&&(in.containsKey("service_uuid")||in.containsKey("characteristics")))return "ble_setting_not_supported_in_mode";
 out["name"]=name;out["mode"]=mode;
 if(!strcmp(mode,"hid")){const char*h=in["report_map_hex"]|"";size_t n=strlen(h);if(!n||n%2||n>1024)return "hid_report_map_required";for(size_t i=0;i<n;++i)if(!isxdigit((unsigned char)h[i]))return "invalid_hex";out["report_map_hex"]=h;}
 if(!strcmp(mode,"gatt")){const char*uuid=in["service_uuid"]|"";auto chars=in["characteristics"].as<JsonArrayConst>();if(!validBleUuid(uuid)||!chars.size()||chars.size()>4)return "gatt_configuration";out["service_uuid"]=uuid;auto target=out["characteristics"].to<JsonArray>();
   for(auto c:chars){if(!c.is<JsonObjectConst>())return "gatt_characteristic";for(auto f:c.as<JsonObjectConst>())if(strcmp(f.key().c_str(),"uuid")&&strcmp(f.key().c_str(),"properties"))return "unknown_gatt_setting";const char*u=c["uuid"]|"";if(!validBleUuid(u)||!c["properties"].is<unsigned>()||!c["properties"].as<unsigned>()||(c["properties"].as<unsigned>()&~26u))return "gatt_characteristic";for(auto old:target)if(!strcasecmp(old["uuid"].as<const char*>(),u))return "duplicate_gatt_uuid";auto t=target.add<JsonObject>();t["uuid"]=u;t["properties"]=c["properties"];}}
 return nullptr;
}
inline const char*mergeBleProfile(JsonObjectConst base,JsonObjectConst patch,JsonObject out){
 JsonDocument source;source.set(base);
 if(patch.containsKey("mode")&&patch["mode"]!=base["mode"]){source.remove("report_map_hex");source.remove("service_uuid");source.remove("characteristics");}
 for(auto f:patch)source[f.key()]=f.value();return normalizeBleProfile(source.as<JsonObjectConst>(),out);
}
class BleConfiguration {
 JsonDocument data_;uint32_t revision_=1,profileRevision_=1;bool valid_=true,persisted_=true,nextBootAutoStart_=false;
 static const char*validate(JsonObjectConst input,JsonObject result){
   for(auto f:input)if(strcmp(f.key().c_str(),"auto_start")&&strcmp(f.key().c_str(),"profile"))return "unknown_ble_configuration";
   if(!input["auto_start"].is<bool>()||!input["profile"].is<JsonObjectConst>())return "ble_config_format";
   result["auto_start"]=input["auto_start"];return normalizeBleProfile(input["profile"],result["profile"].to<JsonObject>());
 }
public:
 using Persist=bool(*)(const char*,size_t);
 BleConfiguration(){data_["auto_start"]=false;data_["profile"]["name"]="Agentic Notes";data_["profile"]["mode"]="central";}
 bool restore(const char*text){JsonDocument d,normalized;if(deserializeJson(d,text)||validate(d.as<JsonObjectConst>(),normalized.to<JsonObject>())){valid_=false;return false;}if(normalized.overflowed()){valid_=false;return false;}swap(data_,normalized);nextBootAutoStart_=autoStart();persisted_=true;valid_=true;return true;}
 const char*configure(JsonObjectConst a,JsonObject out,Persist persist){
   if(a.containsKey("persist")&&!a["persist"].is<bool>())return "boolean_required";
   if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<unsigned>()||a["expected_revision"].as<unsigned>()!=revision_))return "revision_conflict";
   for(auto f:a)if(strcmp(f.key().c_str(),"auto_start")&&strcmp(f.key().c_str(),"profile")&&strcmp(f.key().c_str(),"persist")&&strcmp(f.key().c_str(),"expected_revision"))return "unknown_ble_configuration";
   if(!a.containsKey("auto_start")&&!a.containsKey("profile"))return "changes_required";
   JsonDocument next,normal;next.set(data_);if(a.containsKey("auto_start"))next["auto_start"]=a["auto_start"];if(a.containsKey("profile")){if(!a["profile"].is<JsonObjectConst>())return "ble_profile_type";JsonDocument merged;if(auto e=mergeBleProfile(profile(),a["profile"],merged.to<JsonObject>()))return e;next["profile"]=merged.as<JsonVariantConst>();}
   if(auto e=validate(next.as<JsonObjectConst>(),normal.to<JsonObject>()))return e;
   if(next.overflowed()||normal.overflowed())return "memory_low";
   if(a["persist"]|false){char bytes[2048];size_t n=measureJson(normal);if(n>=sizeof bytes)return "ble_config_limit";serializeJson(normal,bytes,sizeof bytes);if(!persist||!persist(bytes,n))return "persistence_failed";}
   bool profileChanged=differs(normal["profile"].as<JsonObjectConst>());swap(data_,normal);++revision_;if(profileChanged)++profileRevision_;persisted_=a["persist"]|false;if(persisted_)nextBootAutoStart_=autoStart();out["status"]="accepted";out["revision"]=revision_;out["profile_apply_time"]="next_ble_start";out["auto_start_apply_time"]=persisted_?"next_boot":"requires_persistence";return nullptr;
 }
 uint32_t profileRevision()const{return profileRevision_;}
 bool autoStart()const{return data_["auto_start"]|false;}
 JsonObjectConst profile()const{return data_["profile"].as<JsonObjectConst>();}
 void read(JsonObject out)const{out["configuration_revision"]=revision_;out["profile_revision"]=profileRevision_;out["configured"]=data_;out["configuration_persisted"]=persisted_;out["next_boot_auto_start"]=nextBootAutoStart_;out["restore_valid"]=valid_;}
 bool differs(JsonObjectConst active)const{JsonDocument normal;if(normalizeBleProfile(active,normal.to<JsonObject>()))return true;char a[2048],b[2048];serializeJson(normal,a,sizeof a);serializeJson(profile(),b,sizeof b);return strcmp(a,b);}
};
}
