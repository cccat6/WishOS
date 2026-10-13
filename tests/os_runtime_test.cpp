#include "../firmware/AgenticOS/src/Defaults.h"
#include "../firmware/AgenticOS/src/Vfs.h"
#include "../firmware/AgenticOS/src/E6Driver.h"
#include "../firmware/AgenticOS/src/PowerSense.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace notesos;
static JsonDocument j(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static bool ready=true,bus=true;static std::vector<int> commands;static unsigned resets=0,dataWrites=0;
static bool r(){return ready;}static void reset(bool){assert(ready);++resets;}
static bool c(uint8_t op,const uint8_t*,size_t){assert(ready);if(!bus)return false;commands.push_back(op);return true;}
static bool data(const uint8_t*,size_t){assert(ready);if(!bus)return false;++dataWrites;return true;}
static int gateMode=0,calls=0;static int gate(const Event&){return gateMode;}
static const char* handler(const char*,JsonObjectConst,JsonObject){++calls;return nullptr;}
int main(){
 assert(!decodePowerSample(0,false).batteryValid&&decodePowerSample(0,false).externalPower==-1);
 assert(!decodePowerSample(0,true).batteryValid&&decodePowerSample(0,true).externalPower==1);
 assert(decodePowerSample(1600,true).batteryMv==3200&&decodePowerSample(1600,true).externalPower==0);
 Policies p(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));JsonDocument out;
 auto a=j(R"({"expected_revision":1,"changes":[{"resource":"/dev/mic","settings":{"max_duration_ms":null}}]})");
 assert(!p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>(),true));assert(p.revision()==1);
 assert(!p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(p.revision()==2);
 JsonDocument values;p.resolve("/dev/mic","system",values.to<JsonObject>());assert(values["max_duration_ms"].isNull());
 assert(p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()));
 a=j(R"({"changes":[{"resource":"/dev/mic","settings":{"sample_rate_hz":32000}}]})");assert(p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()));
 a=j(R"({"changes":[{"resource":"/dev/mic","settings":{"network_queue":false,"gpio":8}}]})");assert(p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(p.get<bool>("/dev/mic","network_queue",false));
 a=j(R"({"scope":"app:test","changes":[{"resource":"/sys/input","settings":{"during_display_busy":"drop"}}]})");assert(!p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()));p.resolve("/sys/input","app:test",values.to<JsonObject>());assert(values["during_display_busy"]=="drop");p.resolve("/sys/input","system",values.to<JsonObject>());assert(values["during_display_busy"]=="dispatch");
 MemoryFiles mem("ram","/storage/ram","ram");assert(mem.budget(8));size_t n=0;uint8_t b[8]={1,2,3,4};
 assert(!mem.mkdir("/app"));assert(!mem.write("/app/a",0,b,4,false,n)&&n==4);assert(!mem.write("/app/a",6,b,2,false,n));assert(!mem.budget(7));assert(!mem.read("/app/a",0,b,8,n)&&n==8&&b[4]==0&&b[6]==1);
 assert(mem.write("/app/b",0,b,1,false,n));assert(mem.remove("/app"));assert(!mem.rename("/app/a","/app/b"));assert(!mem.remove("/app/b"));assert(!mem.remove("/app"));
 char path[256];assert(normalizePath("/storage/ram/app/pages","../assets/icon.svg",path,sizeof(path),"/storage/ram/app"));assert(!strcmp(path,"/storage/ram/app/assets/icon.svg"));assert(!normalizePath("/storage/ram/app","../../user/private",path,sizeof(path),"/storage/ram/app"));
 a=j(R"({"n":"18446744073709551615"})");uint64_t offset;assert(parseOffset(a["n"],offset)&&offset==UINT64_MAX);a=j(R"({"n":"18446744073709551616"})");assert(!parseOffset(a["n"],offset));
 Core core;core.add({"/in","read","",handler});core.add({"/out","write","",handler});core.setEventGate(gate);
 a=j(R"({"source":"/in","event":"press","target":"/out","method":"write","cooldown_ms":0})");assert(!core.setRule(a.as<JsonObjectConst>(),out.to<JsonObject>()));uint32_t cursor=0;gateMode=1;core.emit("/in","press",a.as<JsonObjectConst>());core.dispatch(cursor);assert(!calls&&core.deferredCount()==1);gateMode=0;core.dispatch(cursor);assert(calls==1&&!core.deferredCount());gateMode=2;core.emit("/in","press",a.as<JsonObjectConst>());core.dispatch(cursor);assert(calls==1&&core.policyDrops()==1);
 E6Driver panel({r,reset,c,data});uint64_t t=100;assert(panel.start(false,t,2000));for(;!panel.receiving()&&t<500;++t)panel.tick(t);assert(panel.receiving());
 std::vector<uint8_t> pixels(5760,0x11);ready=false;assert(!panel.write(pixels.data(),pixels.size()));unsigned before=commands.size();panel.tick(t++);assert(commands.size()==before);ready=true;
 for(int i=0;i<30;++i)assert(panel.write(pixels.data(),pixels.size()));assert(panel.commit(t));assert(commands.back()==4);
 ready=false;for(int i=0;i<20;++i)panel.tick(++t);assert(commands.back()==4);ready=true;for(int i=0;i<8;++i)panel.tick(++t);assert(commands.back()==0x12);
 ready=false;panel.tick(++t);assert(!panel.cancel());before=commands.size();panel.tick(t+3000);assert(panel.faulted()&&commands.size()==before);assert(!panel.clearFault(t+3001));assert(!panel.start(false,t+3002,2000));assert(commands.size()==before);
 ready=true;assert(!panel.clearFault(t+3003));assert(panel.clearFault(t+3006));assert(commands.size()==before);
 commands.clear();E6Driver factory({r,reset,c,data});assert(factory.start(true,5000,2000));for(uint64_t n=5000;n<5200&&!factory.receiving();++n)factory.tick(n);assert(factory.receiving());for(auto cmd:commands)assert(cmd!=0x30);
 std::cout<<"Policy atomicity/scope/null/ranges, volatile VFS/quota/64-bit positions, input gating, display BUSY timeout and SPI transaction boundaries passed\n";
}
