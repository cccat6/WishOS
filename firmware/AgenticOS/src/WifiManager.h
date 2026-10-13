#pragma once
#include "WifiConfig.h"
#include "Core.h"
#include <new>
namespace notesos {
class WifiBackend {
public:
 virtual ~WifiBackend()=default;
 virtual const char*apply(const WifiConfig&)=0;
 virtual bool connected()const=0;
 virtual const char*ssid()=0;
 virtual int rssi()const=0;
 virtual bool switchAllowed(bool allowDisruption)const=0;
 virtual bool startScan()=0;
 virtual int scanState()=0; // -1 running, -2 failed, >=0 number of results
 virtual int signal(const char*ssid)=0; // strongest matching BSSID, -128 if absent
 virtual bool join(const WifiProfile&)=0;
 virtual void disconnect(bool radioOff)=0;
};
class WifiManager {
 WifiBackend&hw_;Core&core_;WifiConfig config_;
 uint32_t revision_=1,persistedRevision_=1;bool restoreOk_=true,dirty_=true,applyForce_=true,suspended_=false,off_=false,paused_=false;
 bool manual_=false,manualReconnect_=true,releaseOnLoss_=false,manualSawLink_=false,disruption_=true;
 bool cancelAttempt_=false,disconnectRequested_=false;uint64_t actionAt_=0;
 bool attempting_=false,attemptManual_=false,scanning_=false,seenLink_=false,scanRequested_=false,selectionPending_=false;
 WifiProfile target_,attempt_;uint64_t holdsUntil_=0,attemptAt_=0,nextAttempt_=0,nextScan_=0,connectedAt_=0,applyAt_=0;
 uint64_t cooldown_[WifiConfig::Capacity]{};int signals_[WifiConfig::Capacity]{};
 char lastSsid_[33]{},reason_[48]="starting",challenger_[25]{};unsigned confirmations_=0;
 void reason(const char*r){copy(reason_,sizeof reason_,r);}
 void emit(const char*event,const WifiProfile*p=nullptr){JsonDocument d;d["mode"]=manual_?"manual":paused_?"paused":"auto";d["reason"]=reason_;if(p){d["profile"]=p->id;d["ssid"]=p->ssid;}else if(hw_.connected()){d["ssid"]=hw_.ssid();}core_.emit("/dev/wifi",event,d.as<JsonObjectConst>());}
 int index(const char*id)const{for(unsigned i=0;i<config_.count;++i)if(!strcmp(config_.profiles[i].id,id))return i;return -1;}
 void configuredSelection(){manual_=!strcmp(config_.selection,"manual");paused_=false;if(manual_)target_=*config_.find(config_.manualProfile);holdsUntil_=0;manualReconnect_=true;releaseOnLoss_=true;manualSawLink_=false;nextAttempt_=0;nextScan_=0;selectionPending_=manual_;}
 void fail(uint64_t now,const char*why){reason(why);emit("attempt_failed",&attempt_);int i=index(attempt_.id);if(i>=0)cooldown_[i]=now+config_.retryMs;
   hw_.disconnect(false);attempting_=false;nextAttempt_=now+config_.retryMs;nextScan_=now;
   if(attemptManual_&&releaseOnLoss_){manual_=false;manualSawLink_=false;nextAttempt_=0;emit("manual_released");}
 }
 void join(const WifiProfile&p,bool manual,uint64_t now){
   bool force=manual?disruption_:config_.roamWithSessions;
   if(hw_.connected()&&!hw_.switchAllowed(force)){reason("network_in_use");selectionPending_=true;return;}
   attempt_=p;attemptManual_=manual;attemptAt_=now;attempting_=true;selectionPending_=false;
   reason(manual?"manual_request":"automatic_selection");emit("attempt_started",&p);
   if(!hw_.join(p))fail(now,"begin_failed");
 }
 void select(uint64_t now){
   int best=-1;for(unsigned i=0;i<config_.count;++i){auto&p=config_.profiles[i];int r=signals_[i];
     if(!p.enabled||!p.automatic||cooldown_[i]>now||(r==-128&&!p.hidden)||(r!=-128&&r<p.minRssi))continue;
     if(best<0||p.priority>config_.profiles[best].priority||(p.priority==config_.profiles[best].priority&&(r>signals_[best]||(r==signals_[best]&&strcmp(p.id,config_.profiles[best].id)<0))))best=i;
   }
   if(best<0){reason("no_eligible_network");challenger_[0]=0;confirmations_=0;return;}
   auto&candidate=config_.profiles[best];
   if(!hw_.connected()){join(candidate,false,now);return;}
   if(!strcmp(candidate.ssid,hw_.ssid())){challenger_[0]=0;confirmations_=0;reason("connected");return;}
   if(!config_.preferPriority||now-connectedAt_<config_.dwellMs)return;
   int currentPriority=-32768;for(unsigned i=0;i<config_.count;++i)if(!strcmp(config_.profiles[i].ssid,hw_.ssid()))currentPriority=config_.profiles[i].priority;
   bool better=candidate.priority>currentPriority||(config_.roamSamePriority&&candidate.priority==currentPriority&&signals_[best]>=hw_.rssi()+config_.margin);
   // Do not disrupt an established link to speculate that a hidden network is present.
   if(!better||signals_[best]==-128){challenger_[0]=0;confirmations_=0;return;}
   if(strcmp(challenger_,candidate.id)){strcpy(challenger_,candidate.id);confirmations_=1;}else ++confirmations_;
   if(confirmations_>=config_.confirmations)join(candidate,false,now);
 }
public:
 using Persist=bool(*)(const char*,size_t);
 WifiManager(WifiBackend&h,Core&c):hw_(h),core_(c){}
 bool scanning()const{return scanning_;}
 const WifiConfig&config()const{return config_;}uint32_t revision()const{return revision_;}
 bool restore(const char*text){JsonDocument d;if(deserializeJson(d,text)){restoreOk_=false;return false;}auto candidate=new(std::nothrow) WifiConfig;
   if(!candidate){restoreOk_=false;return false;}auto e=WifiConfig::parse(d.as<JsonObjectConst>(),*candidate);if(!e){config_=*candidate;configuredSelection();dirty_=true;}delete candidate;restoreOk_=!e;return !e;
 }
 const char*configure(JsonObjectConst a,JsonObject out,Persist persist=nullptr){
   for(auto f:a)if(!Policies::choice("settings,profiles,persist,expected_revision,allow_disruption",f.key().c_str()))return "unknown_wifi_field";
   for(auto key:{"persist","allow_disruption"})if(a.containsKey(key)&&!a[key].is<bool>())return "boolean_required";
   if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
   if(a.containsKey("settings")&&!a["settings"].is<JsonObjectConst>())return "settings_object_required";
   if(a.containsKey("profiles")&&!a["profiles"].is<JsonArrayConst>())return "profiles_array_required";
   if(!a.containsKey("settings")&&!a.containsKey("profiles"))return "changes_required";
   JsonDocument next;config_.write(next.to<JsonObject>(),true);
   for(auto p:a["settings"].as<JsonObjectConst>())next["settings"][p.key()]=p.value();
   if(a.containsKey("profiles"))next["profiles"]=a["profiles"];
   auto candidate=new(std::nothrow) WifiConfig;if(!candidate)return "memory_low";
   if(auto e=WifiConfig::parse(next.as<JsonObjectConst>(),*candidate)){delete candidate;return e;}
   if(measureJson(next)>4096){delete candidate;return "wifi_config_limit";}
   if(a["persist"]|false){if(!persist){delete candidate;return "persistence_unavailable";}char*bytes=(char*)malloc(measureJson(next)+1);if(!bytes){delete candidate;return "memory_low";}
     size_t n=serializeJson(next,bytes,measureJson(next)+1);bool ok=persist(bytes,n);free(bytes);if(!ok){delete candidate;return "persistence_failed";}}
   bool radioChanged=candidate->enabled!=config_.enabled||candidate->apEnabled!=config_.apEnabled||strcmp(candidate->apSsid,config_.apSsid)||strcmp(candidate->apPassword,config_.apPassword)||candidate->apChannel!=config_.apChannel||candidate->apClients!=config_.apClients||candidate->powerDbm!=config_.powerDbm||strcmp(candidate->powerSave,config_.powerSave);
   auto proposed=a["settings"].as<JsonObjectConst>();bool selectionChange=proposed.containsKey("selection")||(!strcmp(candidate->selection,"manual")&&proposed.containsKey("manual_profile"));
   const auto*pendingProfile=attempt_.id[0]?candidate->find(attempt_.id):nullptr;
   bool cancelInFlight=attempting_&&(!candidate->enabled||(!attemptManual_&&!candidate->autoConnect)||selectionChange||
     (attempt_.id[0]&&(!pendingProfile||!pendingProfile->enabled||(!attemptManual_&&!pendingProfile->automatic)||strcmp(pendingProfile->ssid,attempt_.ssid)||strcmp(pendingProfile->password,attempt_.password))));
   uint64_t preserved[WifiConfig::Capacity]{};for(unsigned i=0;i<candidate->count;++i){int old=index(candidate->profiles[i].id);if(old>=0&&!strcmp(candidate->profiles[i].ssid,config_.profiles[old].ssid)&&!strcmp(candidate->profiles[i].password,config_.profiles[old].password))preserved[i]=cooldown_[old];}
   config_=*candidate;delete candidate;++revision_;if(a["persist"]|false)persistedRevision_=revision_;memcpy(cooldown_,preserved,sizeof cooldown_);nextScan_=0;applyAt_=core_.now()+250;
   if(radioChanged||proposed.containsKey("tx_power_dbm")||proposed.containsKey("power_save")){dirty_=true;applyForce_=a["allow_disruption"]|true;}
   auto settings=a["settings"].as<JsonObjectConst>();
   if(settings.containsKey("enabled")){off_=false;dirty_=true;}
   if(settings.containsKey("selection")||(!strcmp(config_.selection,"manual")&&settings.containsKey("manual_profile"))){configuredSelection();disruption_=a["allow_disruption"]|true;nextAttempt_=core_.now()+250;}
   else if(manual_&&target_.id[0]){auto p=config_.find(target_.id);if(p&&p->enabled)target_=*p;else{manual_=false;paused_=true;target_={};reason("manual_profile_removed");}}
   if(cancelInFlight){cancelAttempt_=true;selectionPending_=manual_;actionAt_=core_.now()+250;}
   reason("configuration_updated");emit("configuration_changed");out["revision"]=revision_;out["status"]="accepted";out["apply_time"]="next_safe_poll";return nullptr;
 }
 static const char*validateManual(JsonObjectConst a){
   for(auto k:{"reconnect","allow_disruption","persist"})if(a.containsKey(k)&&!a[k].is<bool>())return "boolean_required";
   const char*hold=a["hold"]|"until_disconnect";if(!Policies::choice("until_resume,until_disconnect,timed",hold))return "manual_hold";
   if(!strcmp(hold,"timed")&&(!a["hold_ms"].is<uint32_t>()||!a["hold_ms"].as<uint32_t>()||a["hold_ms"].as<uint32_t>()>86400000))return "manual_hold_range";
   return nullptr;
 }
 const char*connect(const WifiProfile&p,JsonObjectConst a,uint64_t now){
   if(!config_.enabled)return "wifi_disabled";if(!p.enabled)return "profile_disabled";if(auto e=validateManual(a))return e;
   const char*hold=a["hold"]|"until_disconnect";
   cancelAttempt_=attempting_;actionAt_=now+250;manual_=true;target_=p;paused_=false;dirty_=dirty_||off_;off_=false;challenger_[0]=0;confirmations_=0;applyForce_=a["allow_disruption"]|true;disruption_=applyForce_;selectionPending_=true;
   releaseOnLoss_=!strcmp(hold,"until_disconnect");holdsUntil_=!strcmp(hold,"timed")?now+a["hold_ms"].as<uint32_t>():0;manualReconnect_=a["reconnect"]|true;manualSawLink_=false;nextAttempt_=now+250;
   applyAt_=now+250;reason("manual_selected");emit("selection_changed",&p);return nullptr;
 }
 void resumeAuto(uint64_t now){cancelAttempt_=attempting_;actionAt_=now+250;manual_=false;paused_=false;dirty_=dirty_||off_;off_=false;selectionPending_=false;holdsUntil_=0;challenger_[0]=0;confirmations_=0;nextScan_=now+250;applyAt_=now+250;reason("automatic_selected");emit("selection_changed");}
 void disconnect(bool radioOff,bool resume,uint64_t now){manual_=false;attempting_=false;paused_=!resume;off_=radioOff;selectionPending_=false;nextScan_=now+250;applyAt_=now+250;dirty_=radioOff;disconnectRequested_=true;actionAt_=now+250;reason(radioOff?"radio_off":"explicit_disconnect");emit("selection_changed");}
 void requestScan(){scanRequested_=true;nextScan_=0;}
 void suspend(){suspended_=true;attempting_=false;scanning_=false;hw_.disconnect(true);seenLink_=false;}
 void resume(){suspended_=false;dirty_=true;applyAt_=0;nextScan_=0;nextAttempt_=0;}
 void poll(uint64_t now){
   if(suspended_)return;
   if((disconnectRequested_||cancelAttempt_)&&now>=actionAt_){hw_.disconnect(off_);attempting_=false;scanning_=false;seenLink_=false;disconnectRequested_=cancelAttempt_=false;}
   if(disconnectRequested_||cancelAttempt_)return;
   if(dirty_&&now>=applyAt_){if(hw_.connected()&&!hw_.switchAllowed(applyForce_)){reason("network_in_use");return;}
     if(off_){hw_.disconnect(true);dirty_=false;return;}
     if(auto e=hw_.apply(config_)){reason(e);applyAt_=now+config_.retryMs;emit("configuration_failed");return;}dirty_=false;emit("configuration_applied");}
   if(off_||!config_.enabled||dirty_){reason(dirty_?"configuration_pending":off_?"radio_off":"radio_disabled");return;}
   bool link=hw_.connected();char observed[33]{};if(link)copy(observed,sizeof observed,hw_.ssid());const char*ssid=observed;
   if(link&&(!seenLink_||strcmp(ssid,lastSsid_))){seenLink_=true;strcpy(lastSsid_,ssid);connectedAt_=now;reason("connected");emit("connected");}
   if(!link&&seenLink_){seenLink_=false;lastSsid_[0]=0;reason("link_lost");emit("disconnected");nextScan_=now;nextAttempt_=now;
     if(manual_&&manualSawLink_&&releaseOnLoss_){manual_=false;manualSawLink_=false;emit("manual_released");}}
   if(manual_&&holdsUntil_&&now>=holdsUntil_){manual_=false;selectionPending_=false;holdsUntil_=0;nextScan_=0;
     if(attempting_&&attemptManual_&&(!link||strcmp(ssid,attempt_.ssid))){hw_.disconnect(false);attempting_=false;link=false;observed[0]=0;}
     reason("manual_hold_expired");emit("manual_released");}
   if(attempting_){if(link&&!strcmp(ssid,attempt_.ssid)){attempting_=false;nextAttempt_=now;int i=index(attempt_.id);if(i>=0)cooldown_[i]=0;}
     else if(now-attemptAt_>=config_.connectMs){fail(now,"connect_timeout");return;}else return;}
   if(scanning_){int n=hw_.scanState();if(n==-1)return;scanning_=false;nextScan_=now+config_.scanMs;
     for(unsigned i=0;i<config_.count;++i)signals_[i]=n<0?-128:hw_.signal(config_.profiles[i].ssid);
     if(n<0){reason("scan_failed");emit("scan_failed");}else emit("scan_completed");
     if(!manual_&&!paused_&&config_.autoConnect)select(now);
   }
   if(manual_){if(link&&!strcmp(ssid,target_.ssid)){manualSawLink_=true;selectionPending_=false;reason("manual_connected");}
     else if((selectionPending_||manualReconnect_)&&now>=nextAttempt_){join(target_,true,now);return;}
     else reason("manual_waiting");
   }
   bool eligible=false;for(unsigned i=0;i<config_.count;++i)eligible|=config_.profiles[i].enabled&&config_.profiles[i].automatic;
   bool autoScan=eligible&&!manual_&&!paused_&&config_.autoConnect&&(!link||config_.preferPriority);
   if((scanRequested_||autoScan)&&!scanning_&&now>=nextScan_){scanRequested_=false;if(hw_.startScan()){scanning_=true;reason("scanning");}else{nextScan_=now+config_.retryMs;reason("scan_start_failed");}}
 }
 void read(JsonObject o,bool credentials=false){o["revision"]=revision_;o["configuration_persisted"]=revision_==persistedRevision_;o["restore_valid"]=restoreOk_;o["capacity"]=WifiConfig::Capacity;config_.write(o["configured"].to<JsonObject>(),credentials);
   o["selection"]=off_?"off":paused_?"paused":manual_?"manual":"auto";o["manual_profile"]=manual_?target_.id:"";o["manual_ssid"]=manual_?target_.ssid:"";o["manual_reconnect"]=manualReconnect_;o["manual_hold"]=manual_?(holdsUntil_?"timed":releaseOnLoss_?"until_disconnect":"until_resume"):"";o["hold_until_ms"]=holdsUntil_;o["configuration_pending"]=dirty_;o["connection_pending"]=attempting_||selectionPending_;o["attempt_profile"]=attempting_?attempt_.id:"";o["reason"]=reason_;o["scanning"]=scanning_;o["suspended"]=suspended_;
   auto failures=o["retry_after_ms"].to<JsonObject>();for(unsigned i=0;i<config_.count;++i)if(cooldown_[i]>core_.now())failures[config_.profiles[i].id]=cooldown_[i]-core_.now();
 }
};
}
