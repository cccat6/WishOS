#include "drivers/esp-ledc-backlight/EspLedcBacklight.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static bool persisted=true;static unsigned saves=0;
static bool save(const char*,size_t){++saves;return persisted;}
static JsonDocument json(const char*s){JsonDocument out;assert(!deserializeJson(out,s));return out;}
static void scenario(bool activeLow,int pin){
 backlightCalls=0;backlightFailure=0;backlightTimers[0].freq_hz=24000000;backlightChannels[0].duty=1;
 const Setting settings[]={drivers::EspLedcBacklight::setting()};Policies policies(settings,1);
 drivers::EspLedcBacklight light({pin,activeLow,LEDC_LOW_SPEED_MODE,1,2,20000,10,LEDC_USE_APB_CLK});JsonDocument state;
 auto calls=backlightCalls;light.read(policies,state.to<JsonObject>());light.describe(policies,state["description"].to<JsonObject>());assert(backlightCalls==calls&&state["brightness_percent"]==50&&state["applied_brightness_percent"]==0);
 assert(!light.holdOff());assert(backlightLevels[pin]==(activeLow?1:0));assert(!light.initialize());assert(backlightLevels[pin]==(activeLow?1:0)&&backlightChannels[2].duty==0&&bool(backlightChannels[2].flags.output_invert)==activeLow);assert(backlightTimers[1].clk_cfg==LEDC_USE_APB_CLK&&backlightTimers[0].freq_hz==24000000&&backlightChannels[0].duty==1);
 assert(ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE,2,512,0)!=ESP_OK);
 assert(!light.poll(policies));assert(backlightChannels[2].duty==0);light.frameReady();assert(!light.poll(policies));assert(backlightChannels[2].duty==0);light.panelReady(true);assert(!light.poll(policies));assert(backlightChannels[2].duty==0);light.read(policies,state.to<JsonObject>());assert(state["panel_ready"]==true&&state["frame_ready"]==false&&state["deferred"]==true);light.frameReady();assert(!light.poll(policies));assert(backlightChannels[2].duty==512);
 auto a=json(R"({"brightness_percent":80,"persist":true})");assert(!light.invoke("configure",a.as<JsonObjectConst>(),state.to<JsonObject>(),policies,save));assert(state["brightness_percent"]==80&&state["applied_brightness_percent"]==80&&backlightChannels[2].duty==818);
 auto revision=policies.revision();calls=backlightCalls;
 for(auto bad:{R"({"brightness_percent":101})",R"({"brightness_percent":-1})",R"({"brightness_percent":50.5})",R"({"brightness_percent":true})",R"({"brightness_percent":null})",R"({"channel":0})",R"({"inherit":true,"brightness_percent":20})"}){auto args=json(bad);assert(light.invoke("configure",args.as<JsonObjectConst>(),state.to<JsonObject>(),policies,save));assert(policies.revision()==revision&&backlightCalls==calls);}
 persisted=false;a=json(R"({"brightness_percent":20,"persist":true})");assert(light.invoke("configure",a.as<JsonObjectConst>(),state.to<JsonObject>(),policies,save));assert(policies.revision()==revision&&backlightCalls==calls);persisted=true;
 a=json(R"({"inherit":true})");assert(!light.invoke("configure",a.as<JsonObjectConst>(),state.to<JsonObject>(),policies,save));assert(state["brightness_percent"]==50&&state["applied_brightness_percent"]==50);
 // Restored partial preferences and ordinary system policy use the same source.
 assert(policies.restore(R"({"system":{"/dev/backlight":{"brightness_percent":35}}})"));calls=backlightCalls;light.read(policies,state.to<JsonObject>());assert(backlightCalls==calls&&state["brightness_percent"]==35&&state["applied_brightness_percent"]==50);assert(!light.poll(policies));assert(backlightChannels[2].duty==358);
 light.panelReady(false);assert(!light.poll(policies));assert(backlightChannels[2].duty==0);a=json(R"({"brightness_percent":25})");assert(!light.invoke("configure",a.as<JsonObjectConst>(),state.to<JsonObject>(),policies));assert(state["deferred"]==true&&state["applied_brightness_percent"]==0);light.panelReady(true);assert(!light.poll(policies));assert(backlightChannels[2].duty==0);light.frameReady();assert(!light.poll(policies));assert(backlightChannels[2].duty==256);
 for(int error:{1,2}){backlightChannels[2]={};backlightFailure=error;drivers::EspLedcBacklight failed({pin,activeLow,0,1,2,20000,10,LEDC_USE_APB_CLK});assert(failed.initialize());failed.panelReady(false);calls=backlightCalls;failed.read(policies,state.to<JsonObject>());assert(backlightCalls==calls&&state["initialized"]==false&&state["applied_brightness_percent"]==0&&backlightLevels[pin]==(activeLow?1:0));}
 backlightFailure=0;assert(!light.initialize());light.read(policies,state.to<JsonObject>());assert(state["panel_ready"]==false&&state["frame_ready"]==false&&state["applied_brightness_percent"]==0&&backlightChannels[2].duty==0);
}
int main(){scenario(true,42);scenario(false,13);std::cout<<"PWM backlight: polarity, off/init gates, defaults/inheritance, persistence, invalid-state preservation and camera timer isolation passed\n";}
