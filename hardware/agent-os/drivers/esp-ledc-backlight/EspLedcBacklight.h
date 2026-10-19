#pragma once
#include "Policy.h"
#include <driver/ledc.h>
#include <driver/gpio.h>
#include <string.h>
namespace notesos::drivers {
class EspLedcBacklight {
public:
 struct Config {int gpio;bool activeLow;ledc_mode_t mode;ledc_timer_t timer;ledc_channel_t channel;unsigned frequency,bits;ledc_clk_cfg_t clock;};
 static constexpr Setting setting(){return Setting{"/dev/backlight","brightness_percent","integer","50","",0,100}.inSystem().at("immediate_or_complete_frame");}
private:
 Config config_;bool initialized_=false,panelReady_=false,frameReady_=false,dirty_=true;unsigned applied_=0,attempted_=101;char error_[64]{};
 const char*failed(esp_err_t error){strncpy(error_,esp_err_to_name(error),sizeof error_-1);return error_;}
public:
 explicit EspLedcBacklight(Config config):config_(config){}
 const char*holdOff(){auto e=gpio_set_level(gpio_num_t(config_.gpio),config_.activeLow?1:0);if(e==ESP_OK)e=gpio_set_direction(gpio_num_t(config_.gpio),GPIO_MODE_OUTPUT);return e==ESP_OK?nullptr:failed(e);}
 const char*initialize(){
  if(initialized_){auto error=ledc_stop(config_.mode,config_.channel,0);if(error!=ESP_OK)return failed(error);}
  initialized_=false;panelReady_=false;frameReady_=false;applied_=0;attempted_=101;dirty_=true;error_[0]=0;
  // Establish the inactive electrical level before enabling GPIO output/PWM.
  if(auto error=holdOff())return error;esp_err_t e;
  ledc_timer_config_t timer{};timer.speed_mode=config_.mode;timer.timer_num=config_.timer;timer.duty_resolution=ledc_timer_bit_t(config_.bits);timer.freq_hz=config_.frequency;timer.clk_cfg=config_.clock;
  if((e=ledc_timer_config(&timer))!=ESP_OK)return failed(e);
  ledc_channel_config_t channel{};channel.gpio_num=config_.gpio;channel.speed_mode=config_.mode;channel.channel=config_.channel;channel.timer_sel=config_.timer;channel.intr_type=LEDC_INTR_DISABLE;channel.flags.output_invert=config_.activeLow;channel.duty=0;
  if((e=ledc_channel_config(&channel))!=ESP_OK){ledc_timer_pause(config_.mode,config_.timer);timer.deconfigure=true;ledc_timer_config(&timer);return failed(e);}
  initialized_=true;return nullptr;
 }
 void panelReady(bool ready){panelReady_=ready;if(!ready)frameReady_=false;dirty_=true;}
 void frameReady(){if(panelReady_){frameReady_=true;dirty_=true;}}
 const char*poll(const Policies&policies){
  if(!initialized_)return *error_?error_:"backlight_not_initialized";
  unsigned target=panelReady_&&frameReady_?policies.get<unsigned>("/dev/backlight","brightness_percent",50):0;
  if(!dirty_&&target==attempted_)return *error_?error_:nullptr;
  dirty_=false;attempted_=target;if(target==applied_&&!*error_)return nullptr;
  uint32_t maximum=(uint32_t(1)<<config_.bits)-1,duty=(target*maximum+50)/100;
  // The device event loop owns this channel. The thread-safe combined SDK
  // API requires the unrelated fade ISR service, so use its serial duty pair.
  auto e=ledc_set_duty(config_.mode,config_.channel,duty);if(e==ESP_OK)e=ledc_update_duty(config_.mode,config_.channel);if(e!=ESP_OK)return failed(e);
  applied_=target;error_[0]=0;return nullptr;
 }
 void read(const Policies&policies,JsonObject out)const{
  auto desired=policies.get<unsigned>("/dev/backlight","brightness_percent",50);out["brightness_percent"]=desired;out["applied_brightness_percent"]=applied_;out["initialized"]=initialized_;out["panel_ready"]=panelReady_;out["frame_ready"]=frameReady_;out["deferred"]=(!panelReady_||!frameReady_)&&desired!=applied_;out["configuration_scope"]="system";out["apply_time"]="immediate_or_complete_frame";out["error"]=error_;
 }
 void describe(const Policies&policies,JsonObject out)const{out["type"]="pwm_backlight";out["methods"]="read,describe,configure";out["read_methods"]="read,describe";out["activation_gate"]="panel_ready_and_complete_frame";out["brightness_unit"]="percent_of_pwm_range";out["inherit"]=true;out["configuration_scope"]="system";out["native_wiring_configuration"]="compiled_device_package";policies.describe(out["settings"].to<JsonArray>(),"/dev/backlight");}
 const char*invoke(const char*method,JsonObjectConst args,JsonObject out,Policies&policies,Policies::Persist persist=nullptr){
  if(!strcmp(method,"read")){read(policies,out);return nullptr;}if(!strcmp(method,"describe")){describe(policies,out);return nullptr;}if(strcmp(method,"configure"))return "unsupported_method";
  for(auto value:args)if(!Policies::choice("brightness_percent,inherit,persist",value.key().c_str()))return "unknown_backlight_setting";
  for(auto key:{"inherit","persist"})if(args.containsKey(key)&&!args[key].is<bool>())return "backlight_boolean_required";
  bool inherit=args["inherit"]|false;if(inherit&&args.containsKey("brightness_percent"))return "setting_and_inherit_conflict";
  if(!inherit&&!args.containsKey("brightness_percent"))return "settings_required";
  JsonDocument request;request["scope"]="system";request["persist"]=args["persist"]|false;auto change=request["changes"].to<JsonArray>().add<JsonObject>();change["resource"]="/dev/backlight";
  if(inherit)change["inherit"].to<JsonArray>().add("brightness_percent");else change["settings"]["brightness_percent"]=args["brightness_percent"];
  if(request.overflowed())return "memory_low";
  if(auto error=policies.apply(request.as<JsonObjectConst>(),out,false,persist))return error;
  dirty_=true;auto error=poll(policies);read(policies,out);return error;
 }
};
}
