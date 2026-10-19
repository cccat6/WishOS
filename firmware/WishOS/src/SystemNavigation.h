#pragma once
#include "Applications.h"
namespace notesos {
// Protected logical input is independent of application gesture configuration.
// Physical edges and debounce timestamps arrive through the device adapter.
class SystemNavigation {
 Core&core_;Applications&apps_;uint32_t available_,home_,voice_;
 uint32_t homeMs_=1800,voiceMs_=600,debounceMs_=30,homeClicks_=0,homeWindowMs_=300;
 uint32_t candidate_=0,stable_=0,consumed_=0,clicks_=0;
 uint64_t changed_=0,homeAt_=0,voiceAt_=0,releasedAt_=0;
 bool homeHeld_=false,voiceHeld_=false,homeFired_=false,voiceFired_=false,homeEligible_=false;
public:
 SystemNavigation(Core&c,Applications&a,uint32_t available,uint32_t home,uint32_t voice):core_(c),apps_(a),available_(available),home_(home),voice_(voice){}
 void poll(uint32_t raw,uint64_t now){
  raw&=available_;consumed_&=~(raw&~stable_);
  if(raw!=candidate_){candidate_=raw;changed_=now;}
  if(now-changed_>=debounceMs_)stable_=candidate_;
  const bool home=home_&&(stable_&home_)==home_,voice=voice_&&(stable_&voice_)==voice_;
  const bool homeRaw=home&&(candidate_&home_)==home_,voiceRaw=voice&&(candidate_&voice_)==voice_;
  if(home&&!homeHeld_){homeAt_=changed_;homeFired_=false;homeEligible_=true;if(clicks_&&homeAt_-releasedAt_>homeWindowMs_)clicks_=0;}
  if(voice&&!voiceHeld_){voiceAt_=changed_;voiceFired_=false;}
  if(homeRaw&&!homeClicks_&&!homeFired_&&now-homeAt_>=homeMs_){homeFired_=true;homeEligible_=false;consumed_|=home_;apps_.requestHome();}
  else if(voiceRaw&&!voiceFired_&&!homeFired_&&now-voiceAt_>=voiceMs_){voiceFired_=true;homeEligible_=false;clicks_=0;consumed_|=voice_;apps_.requestVoice();}
  if(homeHeld_&&!home){
   if(homeClicks_&&homeEligible_&&!voiceFired_){releasedAt_=changed_;if(++clicks_>=homeClicks_){clicks_=0;consumed_|=home_;apps_.requestHome();}}
   homeFired_=false;homeEligible_=false;
  }
  if(voiceHeld_&&!voice){if(voiceFired_)apps_.releaseVoice();voiceFired_=false;}
  homeHeld_=home;voiceHeld_=voice;
  if(!home&&clicks_&&now-releasedAt_>homeWindowMs_)clicks_=0;
 }
 uint32_t consumedButtons()const{return consumed_;}
private:
 const char*configuration(JsonObjectConst a,bool apply){if(core_.callerOwner())return "system_configuration_required";
  for(auto f:a)if(!Core::containsMethod("home_buttons,voice_buttons,home_hold_ms,voice_hold_ms,debounce_ms,home_clicks,home_click_window_ms",f.key().c_str()))return "unknown_navigation_setting";
  for(auto f:a)if(!f.value().is<uint32_t>())return "navigation_setting_type";
  uint32_t home=a["home_buttons"]|home_,voice=a["voice_buttons"]|voice_,hm=a["home_hold_ms"]|homeMs_,vm=a["voice_hold_ms"]|voiceMs_,db=a["debounce_ms"]|debounceMs_,hc=a["home_clicks"]|homeClicks_,cw=a["home_click_window_ms"]|homeWindowMs_;
  if((home&~available_)||(voice&~available_)||(available_&&!home)||!hm||!vm||hm>60000||vm>60000||db>500||db>=hm||db>=vm||hc>8||!cw||cw>2000||(hc&&cw<=db)||(!hc&&home==voice&&home&&hm<=vm))return "navigation_unreachable";
  if(!apply)return nullptr;if(voiceFired_)apps_.releaseVoice();
  home_=home;voice_=voice;homeMs_=hm;voiceMs_=vm;debounceMs_=db;homeClicks_=hc;homeWindowMs_=cw;
  candidate_=stable_=consumed_=clicks_=0;homeHeld_=voiceHeld_=homeFired_=voiceFired_=homeEligible_=false;return nullptr;
 }
public:
 const char*configure(JsonObjectConst a){return configuration(a,true);}
 uint32_t wakeButtons()const{return home_|voice_;}
 const char*check(JsonObjectConst a)const{auto candidate=*this;return candidate.configuration(a,false);}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"configure")){if(auto e=configure(a))return e;}else if(strcmp(m,"read"))return "unsupported_method";
  o["available_buttons"]=available_;o["home_buttons"]=home_;o["voice_buttons"]=voice_;o["home_hold_ms"]=homeMs_;o["voice_hold_ms"]=voiceMs_;o["home_clicks"]=homeClicks_;o["home_click_window_ms"]=homeWindowMs_;o["debounce_ms"]=debounceMs_;o["pressed_buttons"]=stable_;o["voice_held"]=voiceHeld_;o["scope"]="system";o["foreground_can_override"]=false;return nullptr;
 }
 void describe(JsonObject o)const{o["input_source"]="device_adapter_logical_buttons";o["available_buttons"]=available_;o["scope"]="system";o["application_boundary"]="next_input_sample";o["recovery_independent_of_application_gestures"]=true;o["configuration_fields"]="home_buttons,voice_buttons,home_hold_ms,voice_hold_ms,debounce_ms,home_clicks,home_click_window_ms";o["home_click_window_ms_range"]="greater_than_debounce..2000";o["home_clicks_range"]="0(hold),1..8(completed_unclaimed_presses)";o["home_hold_active"]="home_clicks=0";o["home_click_independence"]="application_max_clicks_does_not_disable_home; earlier_application_actions_are_not_undone";o["voice_priority"]="hold_claim_clears_home_click_sequence_and_consumes_current_press_clicks";o["events"].to<JsonArray>().add("voice_released");}
};
}
