#pragma once
#include "Applications.h"
namespace notesos {
// Recovery input is sampled independently of the foreground application's
// gesture policy and event consumption. Logical button bits are supplied by the
// device adapter; this class has no GPIO or screen assumptions.
class SystemNavigation {
 Core&core_;Applications&apps_;uint32_t available_,home_,voice_;uint32_t homeMs_=1800,voiceMs_=600,debounceMs_=30;
 uint32_t candidate_=0,stable_=0;uint64_t changed_=0,homeAt_=0,voiceAt_=0;bool homeHeld_=false,voiceHeld_=false,homeFired_=false,voiceFired_=false;
public:
 SystemNavigation(Core&c,Applications&a,uint32_t available,uint32_t home,uint32_t voice):core_(c),apps_(a),available_(available),home_(home),voice_(voice){}
 void poll(uint32_t raw,uint64_t now){raw&=available_;if(raw!=candidate_){candidate_=raw;changed_=now;}if(now-changed_>=debounceMs_)stable_=candidate_;
  // A release awaiting debounce must never mature an old held action after
  // the event loop was busy rendering or loading a resource.
  bool home=home_&&(stable_&home_)==home_&&(candidate_&home_)==home_,voice=voice_&&(stable_&voice_)==voice_&&(candidate_&voice_)==voice_;
  if(home&&!homeHeld_){homeAt_=now;homeFired_=false;}if(voice&&!voiceHeld_){voiceAt_=now;voiceFired_=false;}
  if(home&&!homeFired_&&now-homeAt_>=homeMs_){homeFired_=true;apps_.requestHome();}
  else if(voice&&!voiceFired_&&!homeFired_&&now-voiceAt_>=voiceMs_){voiceFired_=true;apps_.requestVoice();}
  if(!voice&&voiceHeld_&&voiceFired_){JsonDocument e;e["buttons"]=voice_;core_.emit("/sys/navigation","voice_released",e.as<JsonObjectConst>());}
  if(!home)homeFired_=false;if(!voice)voiceFired_=false;homeHeld_=home;voiceHeld_=voice;
 }
 const char*configure(JsonObjectConst a){if(core_.callerOwner())return "system_configuration_required";
  for(auto f:a)if(!Core::containsMethod("home_buttons,voice_buttons,home_hold_ms,voice_hold_ms,debounce_ms",f.key().c_str()))return "unknown_navigation_setting";
  for(auto f:a)if(!f.value().is<uint32_t>())return "navigation_setting_type";
  uint32_t home=a["home_buttons"]|home_,voice=a["voice_buttons"]|voice_,hm=a["home_hold_ms"]|homeMs_,vm=a["voice_hold_ms"]|voiceMs_,db=a["debounce_ms"]|debounceMs_;
  if((home&~available_)||(voice&~available_)||(available_&&!home)||!hm||!vm||hm>60000||vm>60000||db>500||db>=hm||db>=vm||(home==voice&&home&&hm<=vm))return "navigation_unreachable";
  home_=home;voice_=voice;homeMs_=hm;voiceMs_=vm;debounceMs_=db;candidate_=stable_=0;homeHeld_=voiceHeld_=homeFired_=voiceFired_=false;return nullptr;
 }
 uint32_t wakeButtons()const{return home_|voice_;}
 const char*check(JsonObjectConst a)const{auto candidate=*this;return candidate.configure(a);}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"configure")){if(auto e=configure(a))return e;}else if(strcmp(m,"read"))return "unsupported_method";
  o["available_buttons"]=available_;o["home_buttons"]=home_;o["voice_buttons"]=voice_;o["home_hold_ms"]=homeMs_;o["voice_hold_ms"]=voiceMs_;o["debounce_ms"]=debounceMs_;o["pressed_buttons"]=stable_;o["voice_held"]=voiceHeld_;o["scope"]="system";o["foreground_can_override"]=false;return nullptr;
 }
 void describe(JsonObject o)const{o["input_source"]="device_adapter_logical_buttons";o["available_buttons"]=available_;o["scope"]="system";o["application_boundary"]="next_input_sample";o["recovery_independent_of_application_gestures"]=true;o["events"].to<JsonArray>().add("voice_released");}
};
}
