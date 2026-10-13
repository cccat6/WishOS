#pragma once
#include "Policy.h"
namespace notesos {
// No pins, clock source or RTOS assumptions. The adapter serializes configuration
// and samples. Each gesture snapshots its timing through the final click/release.
struct ButtonTiming {
 uint32_t debounce=25,longPress=800,clickWindow=300,repeat=0,maxClicks=3;
 static const char*merge(JsonObjectConst a,ButtonTiming&c){
  for(auto f:a){const char*k=f.key().c_str();if(!Policies::choice("debounce_ms,long_press_ms,click_window_ms,repeat_ms,max_clicks",k))return "unknown_button_setting";
   if(!f.value().is<uint32_t>())return "button_setting_type";uint32_t n=f.value();
   if(!strcmp(k,"debounce_ms")){if(n<1||n>100)c.debounce=0;else c.debounce=n;}
   else if(!strcmp(k,"long_press_ms"))c.longPress=n;
   else if(!strcmp(k,"click_window_ms"))c.clickWindow=n;
   else if(!strcmp(k,"repeat_ms"))c.repeat=n;else c.maxClicks=n;
  }
  if(!c.debounce||c.longPress<100||c.longPress>60000||c.clickWindow<1||c.clickWindow>2000||c.maxClicks<1||c.maxClicks>8||(c.repeat&&c.repeat<20)||c.repeat>60000)return "button_timing_range";
  return nullptr;
 }
 void describe(JsonObject o)const{o["debounce_ms"]=debounce;o["long_press_ms"]=longPress;o["click_window_ms"]=clickWindow;o["repeat_ms"]=repeat;o["max_clicks"]=maxClicks;}
 bool operator==(const ButtonTiming&o)const{return debounce==o.debounce&&longPress==o.longPress&&clickWindow==o.clickWindow&&repeat==o.repeat&&maxClicks==o.maxClicks;}
};
class ButtonGestures {
 struct Button{char name[17]{};ButtonTiming desired,active;bool raw=false,stable=false,longSent=false;uint32_t changed=0,since=0,released=0,repeatAt=0,sequence=0,presentation=0,clicks=0;};
 Button buttons_[8];size_t count_=0;uint32_t next_=1,revision_=1;bool restoreOk_=true;
 void*context_;void(*send_)(void*,const char*,JsonObjectConst);
 void event(Button&b,const char*name,uint32_t now){JsonDocument d;d["button"]=b.name;d["captured_at_ms"]=now;d["gesture_id"]=b.sequence;d["presentation_id"]=b.presentation;d["held_ms"]=now-b.since;d["long"]=b.longSent;if(!strcmp(name,"click"))d["count"]=b.clicks;send_(context_,name,d.as<JsonObjectConst>());}
public:
 using Persist=bool(*)(const char*,size_t);
 ButtonGestures(std::initializer_list<const char*>names,void*context,void(*send)(void*,const char*,JsonObjectConst)):context_(context),send_(send){for(auto n:names)if(count_<8&&strlen(n)<17)copy(buttons_[count_++].name,17,n);}
 const char*configure(JsonObjectConst a,JsonObject out,Persist persist=nullptr){
  for(auto f:a)if(!Policies::choice("defaults,buttons,persist,expected_revision",f.key().c_str()))return "unknown_button_config";
  if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
  if((a.containsKey("persist")&&!a["persist"].is<bool>())||(a.containsKey("defaults")&&!a["defaults"].is<JsonObjectConst>())||(a.containsKey("buttons")&&!a["buttons"].is<JsonObjectConst>()))return "button_config_type";
  ButtonTiming next[8];for(size_t i=0;i<count_;++i){next[i]=buttons_[i].desired;if(auto e=ButtonTiming::merge(a["defaults"].as<JsonObjectConst>(),next[i]))return e;}
  for(auto f:a["buttons"].as<JsonObjectConst>()){size_t i=0;while(i<count_&&strcmp(buttons_[i].name,f.key().c_str()))++i;if(i==count_)return "unknown_button";if(!f.value().is<JsonObjectConst>())return "button_config_type";if(auto e=ButtonTiming::merge(f.value().as<JsonObjectConst>(),next[i]))return e;}
  JsonDocument saved;for(size_t i=0;i<count_;++i)next[i].describe(saved["buttons"][buttons_[i].name].to<JsonObject>());if(saved.overflowed())return "memory_low";
  if(a["persist"]|false){char text[2048];size_t n=measureJson(saved);if(n>=sizeof text)return "button_config_limit";serializeJson(saved,text,sizeof text);if(!persist||!persist(text,n))return "persistence_failed";}
  for(size_t i=0;i<count_;++i){buttons_[i].desired=next[i];auto&b=buttons_[i];if(!b.raw&&!b.stable&&!b.clicks)b.active=b.desired;}++revision_;read(out);return nullptr;
 }
 void commitConfiguration(const ButtonGestures&prepared){for(size_t i=0;i<count_;++i){auto&b=buttons_[i];b.desired=prepared.buttons_[i].desired;if(!b.raw&&!b.stable&&!b.clicks)b.active=b.desired;}revision_=prepared.revision_;restoreOk_=prepared.restoreOk_;}
 bool restore(const char*s){JsonDocument a,o;restoreOk_=!deserializeJson(a,s)&&a.is<JsonObjectConst>()&&!configure(a.as<JsonObjectConst>(),o.to<JsonObject>());return restoreOk_;}
 void read(JsonObject o)const{o["revision"]=revision_;o["restore_valid"]=restoreOk_;o["apply_time"]="next_gesture";auto all=o["buttons"].to<JsonObject>();for(size_t i=0;i<count_;++i){auto&b=buttons_[i];auto x=all[b.name].to<JsonObject>();x["pressed"]=b.stable;x["pending"]=!(b.desired==b.active);b.desired.describe(x["configured"].to<JsonObject>());b.active.describe(x["applied"].to<JsonObject>());}}
 static void describe(JsonObject o){o["events"]="press,release,long_press,repeat,click";o["click_count_range"]="1..8";o["click_delivery"]="after_window_or_max_clicks";o["long_press_cancels_click"]=true;o["configuration_scope"]="device";o["apply_time"]="next_gesture";o["time_unit"]="ms";o["debounce_ms_range"]="1..100";o["long_press_ms_range"]="100..60000";o["click_window_ms_range"]="1..2000";o["repeat_ms_range"]="0(off),20..60000";}
 void sample(size_t i,bool down,uint32_t now,uint32_t presentation=0){
  if(i>=count_)return;auto&b=buttons_[i];
  if(!b.raw&&!b.stable&&b.clicks&&uint32_t(now-b.released)>=b.active.clickWindow){event(b,"click",now);b.clicks=0;}
  if(!b.raw&&!b.stable&&!b.clicks)b.active=b.desired;
  if(down!=b.raw){b.raw=down;b.changed=now;}
  if(b.stable!=b.raw&&uint32_t(now-b.changed)>=b.active.debounce){b.stable=b.raw;
   if(b.stable){if(!b.clicks){b.sequence=next_++;b.presentation=presentation;}b.since=now;b.longSent=false;event(b,"press",now);}
   else{if(!b.longSent&&uint32_t(b.changed-b.since)>=b.active.longPress){b.longSent=true;b.clicks=0;event(b,"long_press",b.changed);}event(b,"release",now);b.released=now;if(!b.longSent){++b.clicks;if(b.clicks==b.active.maxClicks){event(b,"click",now);b.clicks=0;}}else b.clicks=0;}
  }
  if(b.stable&&b.raw&&!b.longSent&&uint32_t(now-b.since)>=b.active.longPress){b.longSent=true;b.clicks=0;b.repeatAt=now;event(b,"long_press",now);}
  else if(b.stable&&b.raw&&b.longSent&&b.active.repeat&&uint32_t(now-b.repeatAt)>=b.active.repeat){b.repeatAt=now;event(b,"repeat",now);}
 }
};
}
