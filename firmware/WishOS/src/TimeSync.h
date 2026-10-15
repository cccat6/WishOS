#pragma once
#include "Core.h"
#include "Policy.h"
namespace notesos {
inline bool validTimezone(const char*text){
 const char*p=text;auto number=[&](unsigned low,unsigned high){if(!isdigit((unsigned char)*p))return false;unsigned n=0;while(isdigit((unsigned char)*p)){n=n*10+(*p++-'0');if(n>high)return false;}return n>=low;};
 auto name=[&](){unsigned n=0;if(*p=='<'){++p;while(*p&&*p!='>'){if(!isalnum((unsigned char)*p)&&*p!='+'&&*p!='-')return false;++p;++n;}if(*p++!='>')return false;}else while(isalpha((unsigned char)*p)){++p;++n;}return n>=3;};
 auto offset=[&](){if(*p=='+'||*p=='-')++p;if(!number(0,24))return false;if(*p==':'){++p;if(!number(0,59))return false;if(*p==':'){++p;if(!number(0,59))return false;}}return true;};
 if(!name()||!offset())return false;if(!*p)return true;if(!name())return false;if(*p!=','&&!offset())return false;
 for(int i=0;i<2;++i){if(*p++!=','||*p++!='M'||!number(1,12)||*p++!='.'||!number(1,5)||*p++!='.'||!number(0,6))return false;if(*p=='/'){++p;if(!offset())return false;}}
 return !*p;
}
struct TimeSyncConfig {
 char timezone[97]="UTC0";bool enabled=true;uint32_t interval=21600000,retry=60000,timeout=15000;char servers[3][97]={{"pool.ntp.org"},{"time.cloudflare.com"},{"time.google.com"}};
 static const char*merge(JsonObjectConst a,TimeSyncConfig&c){
  for(auto f:a){auto k=f.key().c_str();if(!Policies::choice("enabled,interval_ms,retry_ms,timeout_ms,servers,timezone",k))return "unknown_time_setting";
   if(!strcmp(k,"enabled")){if(!f.value().is<bool>())return "time_setting_type";c.enabled=f.value();}
   else if(!strcmp(k,"timezone")){if(!f.value().is<const char*>()||!strlen(f.value().as<const char*>())||strlen(f.value().as<const char*>())>=sizeof c.timezone)return "timezone_rule";for(auto p=f.value().as<const char*>();*p;++p)if(!isalnum((unsigned char)*p)&&!strchr("<>+-:.,/",*p))return "timezone_rule";if(!validTimezone(f.value()))return "timezone_rule";strcpy(c.timezone,f.value());}
   else if(!strcmp(k,"servers")){auto list=f.value().as<JsonArrayConst>();if(!list.size()||list.size()>3)return "time_server_count";memset(c.servers,0,sizeof c.servers);size_t i=0;for(auto s:list){if(!s.is<const char*>()||!strlen(s.as<const char*>())||strlen(s.as<const char*>())>96)return "time_server_name";for(auto p=s.as<const char*>();*p;++p)if(!isalnum((unsigned char)*p)&&*p!='.'&&*p!='-'&&*p!=':')return "time_server_name";strcpy(c.servers[i++],s.as<const char*>());}}
   else{if(!f.value().is<uint32_t>())return "time_setting_type";auto n=f.value().as<uint32_t>();if(!strcmp(k,"interval_ms"))c.interval=n;else if(!strcmp(k,"retry_ms"))c.retry=n;else c.timeout=n;}
  }
  if(c.interval<15000||c.interval>604800000||c.retry<15000||c.retry>86400000||c.timeout<1000||c.timeout>120000)return "time_setting_range";return nullptr;
 }
 void describe(JsonObject o)const{o["timezone"]=timezone;o["enabled"]=enabled;o["interval_ms"]=interval;o["retry_ms"]=retry;o["timeout_ms"]=timeout;auto a=o["servers"].to<JsonArray>();for(auto&s:servers)if(*s)a.add(s);}
};
struct TimeSyncBackend{virtual ~TimeSyncBackend()=default;virtual const char*start(const TimeSyncConfig&)=0;virtual void stop()=0;virtual int64_t completion()=0;};
class TimeSync {
 Core&core_;TimeSyncBackend&backend_;TimeSyncConfig config_;bool active_=false,requested_=false,restoreOk_=true;uint64_t due_=0,started_=0,lastAt_=0;int64_t lastUnix_=0;uint32_t revision_=1;char error_[64]{};
 void notify(const char*name){JsonDocument d;d["last_sync_unix_s"]=lastUnix_;d["error"]=error_;core_.emit("/services/time",name,d.as<JsonObjectConst>());}
public:
 using Persist=bool(*)(const char*,size_t);
 TimeSync(Core&c,TimeSyncBackend&b):core_(c),backend_(b){}
 const TimeSyncConfig&config()const{return config_;}
 void read(JsonObject o)const{config_.describe(o["configured"].to<JsonObject>());o["revision"]=revision_;o["active"]=active_;o["requested"]=requested_;o["next_attempt_ms"]=due_;o["last_sync_ms"]=lastAt_;o["last_sync_unix_s"]=lastUnix_;o["last_error"]=error_;o["restore_valid"]=restoreOk_;o["apply_time"]="next_poll";}
 bool restore(const char*s){JsonDocument d;TimeSyncConfig c=config_;restoreOk_=!deserializeJson(d,s)&&d.is<JsonObjectConst>()&&!TimeSyncConfig::merge(d.as<JsonObjectConst>(),c);if(restoreOk_)config_=c;return restoreOk_;}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o,uint64_t now,Persist persist=nullptr){
  if(!strcmp(m,"read")){read(o);return nullptr;}
  if(!strcmp(m,"describe")){o["methods"]="read,configure,sync";o["events"]="synchronized,failed";o["scope"]="system";o["apply_time"]="next_poll";o["interval_ms_range"]="15000..604800000";o["retry_ms_range"]="15000..86400000";o["timeout_ms_range"]="1000..120000";o["server_limit"]=3;o["auto_connect_network"]=false;o["timezone_format"]="POSIX_offset_or_explicit_M_rules";o["clock_basis"]="UTC";o["transport"]="UDP_direct";o["proxy_tunnel"]=false;return nullptr;}
  if(!strcmp(m,"sync")){if(active_)return "time_sync_in_progress";requested_=true;due_=now;read(o);return nullptr;}
  if(strcmp(m,"configure"))return "unsupported_method";
  for(auto f:a)if(!Policies::choice("settings,persist,expected_revision",f.key().c_str()))return "unknown_time_config";
  if(!a["settings"].is<JsonObjectConst>()||(a.containsKey("persist")&&!a["persist"].is<bool>()))return "time_config_type";
  if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
  TimeSyncConfig next=config_;if(auto e=TimeSyncConfig::merge(a["settings"].as<JsonObjectConst>(),next))return e;
  JsonDocument saved;next.describe(saved.to<JsonObject>());if(saved.overflowed())return "memory_low";
  if(a["persist"]|false){char text[1024];size_t n=serializeJson(saved,text,sizeof text);if(!persist||!persist(text,n))return "persistence_failed";}
  if(active_)backend_.stop();active_=false;config_=next;++revision_;due_=now;requested_=false;read(o);return nullptr;
 }
 void poll(uint64_t now,bool online){
  if(active_){int64_t seconds=backend_.completion();if(seconds>0){backend_.stop();active_=false;lastUnix_=seconds;lastAt_=now;error_[0]=0;due_=now+config_.interval;notify("synchronized");}
   else if(!online||now-started_>=config_.timeout){backend_.stop();active_=false;copy(error_,sizeof error_,online?"sync_timeout":"network_unavailable");due_=now+config_.retry;notify("failed");}return;}
  if((!config_.enabled&&!requested_)||!online||now<due_)return;
  requested_=false;if(auto e=backend_.start(config_)){copy(error_,sizeof error_,e);due_=now+config_.retry;notify("failed");return;}started_=now;active_=true;
 }
};
}
