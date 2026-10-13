#pragma once
#include "Policy.h"
#include <ctype.h>
#include "DriverRequests.h"
namespace notesos {
struct WifiProfile {
 char id[25]{},ssid[33]{},password[65]{};
 int16_t priority=0;bool enabled=true,automatic=true,hidden=false;int16_t minRssi=-100;
};
struct WifiConfig {
 static constexpr unsigned Capacity=8;
 struct Range{const char*key;unsigned low,high;};
 inline static constexpr Range Ranges[]={{"scan_interval_ms",1000,86400000},{"retry_backoff_ms",100,86400000},{"connect_timeout_ms",1000,120000},{"min_dwell_ms",0,86400000},{"candidate_confirmations",1,10},{"rssi_hysteresis_db",0,50},{"ap_channel",1,13},{"ap_max_clients",1,4}};
 static void schema(JsonObject out){
   out["scope"]="system";out["profile_limit"]=Capacity;out["configuration_method"]="configure";
   auto fields=out["settings"].to<JsonObject>();
   for(auto key:{"enabled","auto_connect","roam_same_priority","roam_with_sessions","ap_enabled"})fields[key]["type"]="boolean";
   for(auto r:Ranges){auto f=fields[r.key].to<JsonObject>();f["type"]="integer";f["minimum"]=r.low;f["maximum"]=r.high;}
   fields["selection"]["choices"]="auto,manual";fields["manual_profile"]["type"]="profile_id";fields["switch_policy"]["choices"]="on_disconnect,prefer_priority";
   fields["power_save"]["choices"]="off,minimum,maximum";fields["tx_power_dbm"]["minimum"]=2;fields["tx_power_dbm"]["maximum"]=20;fields["tx_power_dbm"]["multipleOf"]=0.25;
   for(auto key:{"ap_ssid","ap_password"})fields[key]["type"]="string";
   out["manual_holds"]="until_resume,until_disconnect,timed";out["manual_hold_default"]="until_disconnect";out["ssid_max_bytes"]=32;out["profile_priority_min"]=-1000;out["profile_priority_max"]=1000;out["profile_update"]="omitted_password_preserved_empty_password_is_open";
 }

 WifiProfile profiles[Capacity];unsigned count=0;
 bool enabled=true,autoConnect=true,preferPriority=false,roamSamePriority=false,roamWithSessions=false;
 bool apEnabled=true;char apSsid[33]{},apPassword[64]{};uint8_t apChannel=1,apClients=4;
 char selection[8]="auto",manualProfile[25]{};
 uint32_t scanMs=60000,retryMs=30000,connectMs=15000,dwellMs=60000;uint8_t confirmations=2,margin=8;
 double powerDbm=20;char powerSave[8]="minimum";
 const WifiProfile*find(const char*id)const{for(unsigned i=0;i<count;++i)if(!strcmp(profiles[i].id,id))return &profiles[i];return nullptr;}
 static bool idValid(const char*s){if(!*s||strlen(s)>24)return false;for(;*s;++s)if(!isalnum((unsigned char)*s)&&*s!='_'&&*s!='-')return false;return true;}
 static bool passwordValid(const char*s){size_t n=strlen(s);if(!n)return true;if(n>=8&&n<=63)return true;if(n!=64)return false;for(;*s;++s)if(!isxdigit((unsigned char)*s))return false;return true;}
 static const char*profile(JsonObjectConst o,WifiProfile&p){
   for(auto f:o)if(!Policies::choice("id,ssid,password,priority,enabled,auto_connect,hidden,min_rssi",f.key().c_str()))return "unknown_profile_field";
   if(!o["id"].is<const char*>()||!idValid(o["id"]))return "wifi_profile_id";
   if(!o["ssid"].is<const char*>()||!*o["ssid"].as<const char*>()||strlen(o["ssid"])>32)return "wifi_ssid_limit";
   if(!o["password"].isNull()&&!o["password"].is<const char*>())return "wifi_password_type";
   const char*pass=o["password"]|"";if(!passwordValid(pass))return "wifi_password_length";
   if(o.containsKey("priority")&&(!o["priority"].is<int>()||o["priority"].as<int>()<-1000||o["priority"].as<int>()>1000))return "wifi_priority_range";
   if(o.containsKey("min_rssi")&&(!o["min_rssi"].is<int>()||o["min_rssi"].as<int>()<-127||o["min_rssi"].as<int>()>-1))return "wifi_rssi_range";
   for(auto k:{"enabled","auto_connect","hidden"})if(o.containsKey(k)&&!o[k].is<bool>())return "boolean_required";
   strcpy(p.id,o["id"]);strcpy(p.ssid,o["ssid"]);strcpy(p.password,pass);p.priority=o["priority"]|0;p.enabled=o["enabled"]|true;p.automatic=o["auto_connect"]|true;p.hidden=o["hidden"]|false;p.minRssi=o["min_rssi"]|-100;return nullptr;
 }
 void write(JsonObject o,bool secrets=false)const{
   o["version"]=1;auto s=o["settings"].to<JsonObject>();s["enabled"]=enabled;s["auto_connect"]=autoConnect;s["selection"]=selection;s["manual_profile"]=manualProfile;
   s["switch_policy"]=preferPriority?"prefer_priority":"on_disconnect";s["roam_same_priority"]=roamSamePriority;s["roam_with_sessions"]=roamWithSessions;
   s["scan_interval_ms"]=scanMs;s["retry_backoff_ms"]=retryMs;s["connect_timeout_ms"]=connectMs;s["min_dwell_ms"]=dwellMs;s["candidate_confirmations"]=confirmations;s["rssi_hysteresis_db"]=margin;
   s["ap_enabled"]=apEnabled;s["ap_ssid"]=apSsid;s["ap_channel"]=apChannel;s["ap_max_clients"]=apClients;
   if(secrets)s["ap_password"]=apPassword;else s["ap_has_password"]=bool(apPassword[0]);s["tx_power_dbm"]=powerDbm;s["power_save"]=powerSave;
   auto list=o["profiles"].to<JsonArray>();for(unsigned i=0;i<count;++i){auto p=list.add<JsonObject>();auto&v=profiles[i];p["id"]=v.id;p["ssid"]=v.ssid;p["priority"]=v.priority;p["enabled"]=v.enabled;p["auto_connect"]=v.automatic;p["hidden"]=v.hidden;p["min_rssi"]=v.minRssi;if(secrets)p["password"]=v.password;else p["has_password"]=bool(v.password[0]);}
 }
 static const char*parse(JsonObjectConst d,WifiConfig&c){
   if(!d["version"].is<unsigned>()||d["version"].as<unsigned>()!=1||!d["settings"].is<JsonObjectConst>()||!d["profiles"].is<JsonArrayConst>())return "wifi_config_format";
   for(auto f:d)if(!Policies::choice("version,settings,profiles",f.key().c_str()))return "unknown_wifi_field";
   auto s=d["settings"].as<JsonObjectConst>();for(auto f:s)if(!Policies::choice("enabled,auto_connect,selection,manual_profile,switch_policy,roam_same_priority,roam_with_sessions,scan_interval_ms,retry_backoff_ms,connect_timeout_ms,min_dwell_ms,candidate_confirmations,rssi_hysteresis_db,ap_enabled,ap_ssid,ap_password,ap_channel,ap_max_clients,tx_power_dbm,power_save",f.key().c_str()))return "unknown_wifi_setting";
   for(auto k:{"enabled","auto_connect","roam_same_priority","roam_with_sessions","ap_enabled"})if(!s[k].is<bool>())return "boolean_required";
   auto range=[&](const char*k,unsigned lo,unsigned hi){return s[k].is<unsigned>()&&s[k].as<unsigned>()>=lo&&s[k].as<unsigned>()<=hi;};
   for(auto r:Ranges)if(!range(r.key,r.low,r.high))return "wifi_setting_range";
   if(!Policies::choice("auto,manual",s["selection"]|"")||!Policies::choice("on_disconnect,prefer_priority",s["switch_policy"]|""))return "wifi_selection";
   if(!s["manual_profile"].is<const char*>()||strlen(s["manual_profile"])>24||!s["ap_ssid"].is<const char*>()||strlen(s["ap_ssid"])>32||!s["ap_password"].is<const char*>()||strlen(s["ap_password"])>63||!passwordValid(s["ap_password"]))return "wifi_ap_or_profile_limit";
   JsonDocument tuning;tuning["tx_power_dbm"]=s["tx_power_dbm"];tuning["power_save"]=s["power_save"];WifiChange change;if(auto e=validateWifiChange(tuning.as<JsonObjectConst>(),change))return e;
   c.enabled=s["enabled"];c.autoConnect=s["auto_connect"];c.preferPriority=!strcmp(s["switch_policy"],"prefer_priority");c.roamSamePriority=s["roam_same_priority"];c.roamWithSessions=s["roam_with_sessions"];
   strcpy(c.selection,s["selection"]);strcpy(c.manualProfile,s["manual_profile"]);strcpy(c.apSsid,s["ap_ssid"]);strcpy(c.apPassword,s["ap_password"]);c.apEnabled=s["ap_enabled"];c.apChannel=s["ap_channel"];c.apClients=s["ap_max_clients"];
   c.scanMs=s["scan_interval_ms"];c.retryMs=s["retry_backoff_ms"];c.connectMs=s["connect_timeout_ms"];c.dwellMs=s["min_dwell_ms"];c.confirmations=s["candidate_confirmations"];c.margin=s["rssi_hysteresis_db"];c.powerDbm=s["tx_power_dbm"];strcpy(c.powerSave,s["power_save"]);
   auto profiles=d["profiles"].as<JsonArrayConst>();if(profiles.size()>Capacity)return "wifi_profile_capacity";c.count=0;
   for(auto p:profiles){if(!p.is<JsonObjectConst>())return "wifi_profile_type";if(auto e=profile(p,c.profiles[c.count]))return e;
     for(unsigned i=0;i<c.count;++i)if(!strcmp(c.profiles[i].id,c.profiles[c.count].id)||!strcmp(c.profiles[i].ssid,c.profiles[c.count].ssid))return "duplicate_wifi_profile";++c.count;}
   if(!strcmp(c.selection,"manual")){auto p=c.find(c.manualProfile);if(!p||!p->enabled)return "manual_profile_unavailable";}return nullptr;
 }
};
}
