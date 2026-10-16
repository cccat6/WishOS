static std::atomic<unsigned> wifiDisconnectReason{0};
static bool wifiEventsRegistered=false;
// Arduino adapter for the portable connection-policy state machine.
class BoardWifi:public WifiBackend {
 bool apStarted_=false;char apName_[33]{},apPass_[64]{};uint8_t apChannel_=0,apClients_=0;
 String observedSsid_;WifiProfile queued_;bool joinQueued_=false;
public:
 void defaults(WifiConfig&c)const override{EspWifiCapabilities::defaults(c);}
 const char*validate(const WifiConfig&c)const override{return EspWifiCapabilities::validate(c);}
 void describeConfiguration(JsonObject fields)const override{EspWifiCapabilities::describe(fields);}
 const char*apply(const WifiConfig&c)override{
   WiFi.persistent(false);WiFi.setAutoReconnect(false);
   if(!c.enabled){disconnect(true);return nullptr;}
   wifi_mode_t mode=c.apEnabled?WIFI_AP_STA:WIFI_STA;
   if(WiFi.getMode()!=mode&&!WiFi.mode(mode))return "wifi_mode_failed";
   const char*name=c.apSsid[0]?c.apSsid:apSsid;
   if(c.apEnabled&&(!apStarted_||strcmp(apName_,name)||strcmp(apPass_,c.apPassword)||apChannel_!=c.apChannel||apClients_!=c.apClients)){
     if(apStarted_){dns.stop();WiFi.softAPdisconnect(false);apStarted_=false;}
     if(!WiFi.softAP(name,c.apPassword[0]?c.apPassword:nullptr,c.apChannel,0,c.apClients))return "wifi_ap_failed";
     copy(apName_,sizeof apName_,name);copy(apPass_,sizeof apPass_,c.apPassword);apChannel_=c.apChannel;apClients_=c.apClients;apStarted_=true;dns.start(53,"*",WiFi.softAPIP());
   }else if(!c.apEnabled&&apStarted_){dns.stop();WiFi.AP.end();apStarted_=false;}
   if(esp_wifi_set_max_tx_power(int8_t(lround(c.powerDbm*4)))!=ESP_OK)return "wifi_power_failed";
   wifi_ps_type_t ps=!strcmp(c.powerSave,"off")?WIFI_PS_NONE:!strcmp(c.powerSave,"minimum")?WIFI_PS_MIN_MODEM:WIFI_PS_MAX_MODEM;
   return esp_wifi_set_ps(ps)==ESP_OK?nullptr:"wifi_ps_failed";
 }
 void service(){if(joinQueued_&&WiFi.status()!=WL_CONNECTED){joinQueued_=false;WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);WiFi.begin(queued_.ssid,queued_.password[0]?queued_.password:nullptr);}}
 bool connected()const override{return WiFi.status()==WL_CONNECTED;}
 const char*ssid()override{observedSsid_=WiFi.SSID();return observedSsid_.c_str();}
 int rssi()const override{return WiFi.RSSI();}
 bool switchAllowed(bool disruption)const override{return disruption||(!networkActive()&&!nfsService.active()&&!applicationNetworkActive());}
 bool startScan()override{WiFi.scanDelete();return WiFi.scanNetworks(true,true)!=WIFI_SCAN_FAILED;}
 int scanState()override{return WiFi.scanComplete();}
 int signal(const char*ssid)override{int best=-128,n=WiFi.scanComplete();for(int i=0;i<n;++i)if(WiFi.SSID(i)==ssid&&WiFi.RSSI(i)>best)best=WiFi.RSSI(i);return best;}
 bool join(const WifiProfile&p)override{queued_=p;joinQueued_=true;WiFi.setAutoReconnect(false);WiFi.disconnectAsync(false,false);return true;}
 void disconnect(bool off)override{joinQueued_=false;WiFi.setAutoReconnect(false);esp_wifi_scan_stop();WiFi.scanDelete();WiFi.disconnectAsync(off,false);if(off){dns.stop();WiFi.mode(WIFI_OFF);apStarted_=false;}}
};
static BoardWifi boardWifi;static WifiManager wifiManager(boardWifi,core);
static bool persistWifi(const char*data,size_t n){return preferences.putString("wifi_profiles",data)==n;}
static void restoreWifi(){
 if(!wifiEventsRegistered){WiFi.onEvent([](WiFiEvent_t event,WiFiEventInfo_t info){if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED)wifiDisconnectReason=info.wifi_sta_disconnected.reason;});wifiEventsRegistered=true;}
 String current=preferences.getString("wifi_profiles","");if(current.length())wifiManager.restore(current.c_str());
}
static const char*profileChange(const char*m,JsonObjectConst a,JsonObject out){
 JsonDocument change,full;wifiManager.config().write(full.to<JsonObject>(),true);change["profiles"]=full["profiles"];
 auto profiles=change["profiles"].as<JsonArray>();const char*id=a["id"]|"";
 if(!WifiConfig::idValid(id))return "wifi_profile_id";
 int found=-1;for(size_t i=0;i<profiles.size();++i)if(!strcmp(profiles[i]["id"]|"",id))found=i;
 if(!strcmp(m,"profile_remove")){if(found<0)return "profile_not_found";profiles.remove(found);}
 else {JsonObject p=found<0?profiles.add<JsonObject>():profiles[found].as<JsonObject>();p["id"]=id;
   for(auto field:a){const char*k=field.key().c_str();if(!strcmp(k,"persist")||!strcmp(k,"expected_revision"))continue;p[k]=field.value();}}
 change["persist"]=a["persist"]|true;if(a.containsKey("expected_revision"))change["expected_revision"]=a["expected_revision"];
 return wifiManager.configure(change.as<JsonObjectConst>(),out,persistWifi);
}
static const char*wifiHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"describe")){wifiManager.describe(out);return nullptr;}
 if(!strcmp(m,"configure")||!strcmp(m,"write")){
   if(!strcmp(m,"configure"))return wifiManager.configure(a,out,persistWifi);
   // Existing write(power/save) calls remain valid; configure owns broader policy.
   WifiChange checked;if(auto e=validateWifiChange(a,checked))return e;JsonDocument change;change["settings"]=a;return wifiManager.configure(change.as<JsonObjectConst>(),out,persistWifi);
 }
 if(!strcmp(m,"profile_set")||!strcmp(m,"profile_remove"))return profileChange(m,a,out);
 if(!strcmp(m,"profiles")){JsonDocument d;wifiManager.config().write(d.to<JsonObject>(),a["include_credentials"]|false);out["profiles"]=d["profiles"];out["revision"]=wifiManager.revision();out["capacity"]=WifiConfig::Capacity;return nullptr;}
 if(!strcmp(m,"connect")){
   if(auto e=WifiManager::validateManual(a))return e;if(!wifiManager.config().enabled)return "wifi_disabled";
   WifiProfile target;const char*id=a["profile"]|"";
   if(*id){auto p=wifiManager.config().find(id);if(!p)return "profile_not_found";target=*p;}
   else {JsonDocument p;const char*ssid=a["ssid"]|"";const WifiProfile*saved=nullptr;
     for(unsigned i=0;i<wifiManager.config().count;++i)if(!strcmp(wifiManager.config().profiles[i].ssid,ssid))saved=&wifiManager.config().profiles[i];
     char generated[25];unsigned number=1;do{snprintf(generated,sizeof generated,"wifi%u",number++);}while(wifiManager.config().find(generated)&&number<=WifiConfig::Capacity+1);
     p["id"]=a["id"]|(saved?saved->id:generated);p["ssid"]=ssid;p["password"]=a["password"]|(saved?saved->password:"");
     if(saved){p["priority"]=saved->priority;p["enabled"]=saved->enabled;p["auto_connect"]=saved->automatic;p["hidden"]=saved->hidden;p["min_rssi"]=saved->minRssi;}
     if(auto e=WifiConfig::profile(p.as<JsonObjectConst>(),target))return e;if(!target.enabled)return "profile_disabled";
     if(a["persist"]|true){p["persist"]=true;JsonDocument r;if(auto e=profileChange("profile_set",p.as<JsonObjectConst>(),r.to<JsonObject>()))return e;}
     else target.id[0]=0;
   }
   if(auto e=wifiManager.connect(target,a,uptime()))return e;out["status"]="accepted";out["ssid"]=target.ssid;out["selection"]="manual";return nullptr;
 }
 if(!strcmp(m,"resume_auto")){wifiManager.resumeAuto(uptime());out["status"]="accepted";return nullptr;}
 if(!strcmp(m,"disconnect")||!strcmp(m,"off")){wifiManager.disconnect(!strcmp(m,"off"),a["resume_auto"]|false,uptime());out["status"]="accepted";return nullptr;}
 if(!strcmp(m,"scan_start")){if(!wifiManager.config().enabled)return "wifi_disabled";wifiManager.requestScan();out["state"]="scan_requested";return nullptr;}
 if(!strcmp(m,"scan_results")){int n=WiFi.scanComplete();out["scanning"]=n==WIFI_SCAN_RUNNING;out["failed"]=n==WIFI_SCAN_FAILED;auto list=out["networks"].to<JsonArray>();uint32_t start=a["offset"]|0u;for(uint32_t i=start;n>=0&&i<uint32_t(n)&&i-start<8;++i){auto net=list.add<JsonObject>();net["ssid"]=WiFi.SSID(i);net["rssi"]=WiFi.RSSI(i);net["channel"]=WiFi.channel(i);net["auth"]=int(WiFi.encryptionType(i));}out["count"]=n<0?0:n;return nullptr;}
 if(!strcmp(m,"scan_clear")){if(wifiManager.scanning())return "scan_in_progress";WiFi.scanDelete();return nullptr;}
 if(strcmp(m,"read"))return "unsupported_method";
 wifiManager.read(out,a["include_credentials"]|false);out["sdk_status"]=int(WiFi.status());out["disconnect_reason"]=wifiDisconnectReason.load();out["enabled"]=WiFi.getMode()!=WIFI_OFF;out["auto_connect"]=wifiManager.config().autoConnect;out["connected"]=WiFi.status()==WL_CONNECTED;out["ssid"]=WiFi.SSID();out["ip"]=WiFi.localIP().toString();out["ap_ip"]=WiFi.softAPIP().toString();out["ap_ssid"]=WiFi.softAPSSID();uint8_t mac[6]{};char macText[18]{};if(esp_read_mac(mac,ESP_MAC_WIFI_STA)==ESP_OK){snprintf(macText,sizeof macText,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);out["mac"]=macText;}else out["mac"]=nullptr;out["radio_mode"]=int(WiFi.getMode());out["ap_active"]=bool(WiFi.getMode()&WIFI_AP);if(WiFi.getMode()!=WIFI_OFF)out["channel"]=WiFi.channel();else out["channel"]=nullptr;
 if(WiFi.status()==WL_CONNECTED)out["rssi"]=WiFi.RSSI();else out["rssi"]=nullptr;
 int8_t p=0;if(esp_wifi_get_max_tx_power(&p)==ESP_OK)out["tx_power_dbm"]=float(p)/4;else out["tx_power_dbm"]=nullptr;
 wifi_ps_type_t ps;if(esp_wifi_get_ps(&ps)==ESP_OK)out["power_save"]=ps==WIFI_PS_NONE?"off":ps==WIFI_PS_MIN_MODEM?"minimum":"maximum";else out["power_save"]=nullptr;
 return nullptr;
}
