#include "../firmware/AgenticOS/src/Conditions.h"
#include "compositions/agentic-notes-e6/Defaults.h"
#include "../firmware/AgenticOS/src/PowerMonitor.h"
#include <cassert>
#include <cmath>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>
using namespace notesos;
static JsonDocument parse(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static double voltage=3900,acceleration=0;static bool voltageValid=true,button=false;static std::string network="offline";static unsigned powerReads=0;
static const char*powerRead(const char*,JsonObjectConst,JsonObject o){++powerReads;if(voltageValid)o["mv"]=voltage;else o["mv"]=nullptr;o["undeclared_value"]=123;return nullptr;}
static const char*buttonRead(const char*,JsonObjectConst,JsonObject o){o["A"]["pressed"]=button;return nullptr;}
static const char*imuRead(const char*,JsonObjectConst,JsonObject o){o["raw"].to<JsonArray>().add(acceleration);return nullptr;}
static const char*netRead(const char*,JsonObjectConst,JsonObject o){o["state"]=network;return nullptr;}
static void prop(JsonObject o,const char*k,const char*t){o["properties"][k]["type"]=t;o["properties"][k]["access"]="r";}
static void genericTests(){
  Core core;Conditions c(core);JsonDocument a,r;
  core.add({"/power","read","",powerRead,[](JsonObject o){prop(o,"mv","number");}});
  core.add({"/keys","read","",buttonRead,[](JsonObject o){prop(o,"A.pressed","boolean");}});
  core.add({"/motion","read","",imuRead,[](JsonObject o){prop(o,"raw.0","number");}});
  core.add({"/network","read","",netRead,[](JsonObject o){prop(o,"state","string");}});
  a=parse(R"({"id":"hidden","source":"/power","property":"undeclared_value","operator":"above","value":1})");assert(c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  a=parse(R"({"id":"low","source":"/power","property":"mv","operator":"below","value":3500,"hysteresis":100,"hold_ms":200,"emit_initial":true})");assert(!c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  a["id"]="high";a["operator"]="above";a["value"]=4000;assert(!c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  c.tick(0);assert(powerReads==1);voltage=3400;c.tick(100);c.tick(200);assert(!c.active("low"));voltageValid=false;c.tick(300);voltageValid=true;c.tick(400);c.tick(500);c.tick(600);assert(c.active("low"));voltage=3590;c.tick(700);c.tick(800);assert(c.active("low"));voltage=3610;c.tick(900);c.tick(1100);assert(!c.active("low"));
  a=parse(R"({"id":"key","source":"/keys","property":"A.pressed","operator":"equal","value":true,"hold_ms":200})");assert(!c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  c.tick(1200);button=true;c.tick(1300);c.tick(1500);assert(c.active("key"));button=false;c.tick(1600);c.tick(1800);assert(!c.active("key"));
  a=parse(R"({"id":"motion","source":"/motion","property":"raw.0","operator":"above","value":10,"hysteresis":2})");assert(!c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));acceleration=11;c.tick(1900);assert(c.active("motion"));acceleration=9;c.tick(2000);assert(c.active("motion"));acceleration=7;c.tick(2100);assert(!c.active("motion"));
  a=parse(R"({"id":"net","source":"/network","property":"state","operator":"change","hold_ms":100})");assert(!c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));c.tick(2200);c.tick(2300);uint32_t sequence=core.sequence();network="online";c.tick(2400);c.tick(2500);assert(core.sequence()==sequence+1);
  core.events(sequence,8,r.to<JsonObject>());assert(r["events"][0]["event"]=="changed"&&r["events"][0]["data"]["id"]=="net");
  a["id"]="system:test";assert(c.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(c.remove("system:test"));
  // Descriptor contracts are separate from any current result values.
  core.describe("/keys",r.to<JsonObject>());assert(r["properties"]["A.pressed"]["type"]=="boolean");assert(r["properties"]["undeclared_value"].isNull());
}
namespace board {
using std::isfinite;
namespace pins {constexpr bool VBUS_CLAMPS_BATTERY_ADC=true;constexpr float BATTERY_DIVIDER=2.0f;}
static Core core;static Conditions conditions(core);static Policies policies(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));
static uint64_t clockMs=0;static uint32_t adc=1950;static float batteryGain=1.0f;static uint32_t cursor=0;static int actions=0;
struct E{std::string name,id;};static std::vector<E> transitions;
static uint64_t uptime(){return clockMs;}static uint32_t readBatteryAdcMv(){return adc;}
static void emit(const char*p,const char*n,JsonObjectConst d){core.emit(p,n,d);}
#include "compositions/agentic-notes-e6/DriverDescriptions.h"
#include "drivers/esp32-services/PowerService.h"
static const char*noop(const char*,JsonObjectConst,JsonObject){return nullptr;}
static void advance(unsigned duration,unsigned value){adc=value;for(unsigned i=0;i<duration;i+=100){clockMs+=100;core.clock(clockMs);pollPowerSense();conditions.tick(clockMs);core.dispatch(cursor);}}
static unsigned count(const char*id,const char*name){unsigned n=0;for(auto&e:transitions)if(e.id==id&&e.name==name)++n;return n;}
static void run(){
  core.add({"/dev/battery","read,write","",batteryHandler,batteryDescription});core.add({"/sys/conditions","read,write,remove","",noop,conditionDescription});
  core.add({"/action","run","",[](const char*,JsonObjectConst,JsonObject)->const char*{++actions;return nullptr;}});
  core.setEventObserver([](const Event&e){if(!strcmp(e.path,"/sys/conditions")){auto d=parse(e.data);transitions.push_back({e.name,d["id"]|""});}return false;});
  JsonDocument a,r;advance(2000,1950);assert(!batteryHandler("read",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["mv"]==3900);
  a=parse(R"({"id":"plug","source":"/dev/battery","property":"external_power","operator":"equal","value":true})");assert(!conditions.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  a=parse(R"({"id":"low","source":"/dev/battery","property":"mv","operator":"below","value":3500,"hold_ms":300,"hysteresis":100,"emit_initial":true})");assert(!conditions.set(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  a=parse(R"({"source":"/sys/conditions","event":"entered","where":{"id":"low"},"target":"/action","method":"run","cooldown_ms":0})");assert(!core.setRule(a.as<JsonObjectConst>(),r.to<JsonObject>()));
  advance(500,1950);advance(1000,0);assert(count("plug","entered")==1);advance(100,1300);advance(100,1600);advance(2000,1950);assert(count("plug","exited")==1&&actions==0);assert(count("system:low-battery","entered")==0);
  advance(5000,1700);assert(actions==1&&count("system:low-battery","entered")==1);advance(2000,1700);assert(actions==1);
  advance(5000,1950);assert(count("system:low-battery","exited")==1);
  JsonDocument old,newer;batteryProperties(old.to<JsonObject>(),false);batteryProperties(newer.to<JsonObject>(),true);assert(old["properties"]["external_power"].isNull());assert(newer["properties"]["external_power"]["type"]=="boolean");
  auto s=powerMonitor.read(clockMs);powerValues(s,old.to<JsonObject>(),false);assert(old["external_power"].isNull());assert(old["supported"].isNull());
  a=parse(R"({"shutdown_mv":3400})");assert(batteryHandler("write",a.as<JsonObjectConst>(),r.to<JsonObject>()));
  assert(!core.find("/dev/power_input"));assert(!core.supports(*core.find("/dev/battery"),"watch"));
}
}
int main(){genericTests();board::run();std::cout<<"Generic driver declarations and shared numeric/boolean/string conditions, grouped reads, hysteresis, ADC filtering and rule actions passed\n";}
