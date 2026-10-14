#pragma once
#include "WifiConfig.h"
namespace notesos {
struct WifiChange{bool power=false,save=false;int8_t quarterDbm=0;int saveMode=0;};
inline const char*validateWifiChange(JsonObjectConst a,WifiChange&c){
  for(auto field:a)if(strcmp(field.key().c_str(),"tx_power_dbm")&&strcmp(field.key().c_str(),"power_save"))return "unknown_driver_setting";
  if(a.containsKey("tx_power_dbm")){
    if(!a["tx_power_dbm"].is<double>())return "wifi_power_type";
    double v=a["tx_power_dbm"].as<double>();if(!isfinite(v)||v<2||v>20)return "wifi_power_range";
    if(v*4!=floor(v*4))return "wifi_power_not_representable";
    c.power=true;c.quarterDbm=int8_t(v*4);
  }
  if(a.containsKey("power_save")){
    if(!a["power_save"].is<const char*>())return "invalid_power_save";
    const char*s=a["power_save"];c.save=true;
    if(!strcmp(s,"off"))c.saveMode=0;else if(!strcmp(s,"minimum"))c.saveMode=1;
    else if(!strcmp(s,"maximum"))c.saveMode=2;else return "invalid_power_save";
  }
  return nullptr;
}

// ESP32 adapter settings/defaults; the portable SSID selection state machine does not
// infer them for another wireless implementation.
struct EspWifiCapabilities {
 static void defaults(WifiConfig&c){c.powerDbm=20;c.apClients=4;strcpy(c.powerSave,"minimum");}
 static const char*validate(const WifiConfig&c){
  JsonDocument settings;settings["tx_power_dbm"]=c.powerDbm;settings["power_save"]=c.powerSave;
  WifiChange change;if(auto e=validateWifiChange(settings.as<JsonObjectConst>(),change))return e;
  if(c.apChannel<1||c.apChannel>13||c.apClients<1||c.apClients>4)return "wifi_ap_hardware_range";
  return nullptr;
 }
 static void describe(JsonObject fields){
  auto power=fields["tx_power_dbm"].to<JsonObject>();power["type"]="number";power["minimum"]=2;power["maximum"]=20;power["multipleOf"]=0.25;power["default"]=20;
  auto save=fields["power_save"].to<JsonObject>();save["type"]="enum";save["choices"]="off,minimum,maximum";save["default"]="minimum";
  for(auto key:{"ap_channel","ap_max_clients"}){auto f=fields[key].to<JsonObject>();f["type"]="integer";f["minimum"]=1;f["maximum"]=!strcmp(key,"ap_channel")?13:4;f["default"]=!strcmp(key,"ap_channel")?1:4;}
 }
};
}
