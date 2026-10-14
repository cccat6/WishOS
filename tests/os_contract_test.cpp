#include "Defaults.h"
#include "CachePath.h"
#include "DriverRequests.h"
#include <cassert>
#include <cstring>
#include <iostream>
using namespace notesos;
static JsonDocument json(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static const char*platform(const Policies&p){return p.get<uint32_t>("/sys/bus","display_spi_hz",0)>20000000?"platform_clock_limit":nullptr;}
struct ObservedFiles:MemoryFiles {
 unsigned stats=0,writes=0;
 ObservedFiles():MemoryFiles("ram","/storage/ram","ram"){}
 const char*stat(const char*p,FileInfo&i)override{++stats;return MemoryFiles::stat(p,i);}
 const char*mkdir(const char*p)override{++writes;return MemoryFiles::mkdir(p);}
};
int main(){
 Policies p(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));p.setValidator(platform);
 JsonDocument out;auto original=p.revision();
 assert(!p.restore(R"({"app:reader":{"/sys/storage":{"ram_quota_bytes":3000}}})"));
 assert(!p.restore(R"({"bad/scope":{"/dev/display":{"queue_limit":3}}})"));
 assert(!p.restore(R"({"system":{"/sys/bus":{"display_spi_hz":24000000}}})"));
 assert(p.get<uint32_t>("/sys/bus","display_spi_hz",0)==16000000);
 auto a=json(R"({"changes":[{"resource":"/dev/display","settings":{"queue_limit":3}},{"resource":"/sys/bus","settings":{"display_spi_hz":24000000}}]})");
 assert(!strcmp(p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()),"platform_clock_limit"));
 assert(p.revision()==original&&p.get<uint32_t>("/dev/display","queue_limit",0)==2);
 assert(p.restore(R"({"system":{"/dev/display":{"queue_limit":3}}})"));
 assert(p.get<uint32_t>("/dev/display","queue_limit",0)==3);
 // Different resources must not leak fields from the previous resolution.
 p.resolve("/dev/display","system",out.to<JsonObject>());p.resolve("/sys/bus","system",out.as<JsonObject>());
 assert(out["queue_limit"].isNull());
 ObservedFiles files;assert(!resolveCachePath(files,"image.1",out.to<JsonObject>()));
 assert(files.stats==0&&files.writes==0&&out["file"]=="/storage/ram/cache/image.1");
 assert(!prepareCacheDirectory(files));assert(files.writes==1);
 assert(!prepareCacheDirectory(files));assert(files.writes==1);
 assert(resolveCachePath(files,"../private",out.to<JsonObject>()));
 files.setPresent(false);FileInfo info;size_t n=9;uint8_t b[1]{};
 assert(files.stat("/",info)&&files.read("/x",0,b,1,n)&&n==0);
 assert(files.mkdir("/x")&&files.remove("/x")&&files.rename("/x","/y"));
 assert(prepareCacheDirectory(files));
 Vfs fs;assert(fs.add(files));assert(fs.lockRead("/storage/ram/cache/a",1));
 assert(fs.hasLeasesUnder("/storage/ram")&&!fs.canMutate("/"));
 assert(fs.canMutate("/storage/ram/cache/ab")&&fs.canMutate("/storage/user"));
 fs.release(1);assert(fs.canMutate("/"));
 WifiChange wifi;ClockChange clock;
 a=json(R"({"tx_power_dbm":12,"power_save":"invalid"})");assert(validateWifiChange(a.as<JsonObjectConst>(),wifi));
 a=json(R"({"tx_power_dbm":12,"power_save":"maximum"})");wifi={};assert(!validateWifiChange(a.as<JsonObjectConst>(),wifi)&&wifi.power&&wifi.save&&wifi.saveMode==2);
 a=json(R"({"cpu_mhz":80,"unix_s":-1})");assert(validateClockChange(a.as<JsonObjectConst>(),clock,{80,160}));
 a=json(R"({"cpu_mhz":80,"unknown":1})");assert(validateClockChange(a.as<JsonObjectConst>(),clock,{80,160}));
 a=json(R"({"tx_power_dbm":12.1})");wifi={};assert(validateWifiChange(a.as<JsonObjectConst>(),wifi));
 a=json(R"({"cpu_mhz":240})");clock={};assert(!validateClockChange(a.as<JsonObjectConst>(),clock,{80,160,240}));assert(validateClockChange(a.as<JsonObjectConst>(),clock,{80,160}));
 a=json(R"({"sample_rate":"48000","storage":"history"})");assert(validateMicArguments(a.as<JsonObjectConst>()));
 a=json(R"({"sample_rate":48000,"max_ms":null,"network_queue":true})");assert(!validateMicArguments(a.as<JsonObjectConst>()));
 a=json(R"({"storage":"unknown"})");assert(validateMicArguments(a.as<JsonObjectConst>()));a=json(R"({"max_ms":0})");assert(validateMicArguments(a.as<JsonObjectConst>()));
 // A delayed stop/read for an earlier recording cannot act on its replacement.
 a=json(R"({"recording_id":"old"})");for(auto method:{"stop","read","read_chunk"})assert(!strcmp(validateMicControl(method,a.as<JsonObjectConst>(),"current"),"recording_replaced"));
 a["recording_id"]="current";assert(!validateMicControl("stop",a.as<JsonObjectConst>(),"current"));
 a["recording_id"]=42;assert(validateMicControl("stop",a.as<JsonObjectConst>(),"current"));
 a=json(R"({"recording_id":"current","limit":16})");assert(!validateMicControl("read_chunk",a.as<JsonObjectConst>(),"current"));assert(validateMicControl("stop",a.as<JsonObjectConst>(),"current"));
 a["limit"]=17;assert(validateMicControl("read_chunk",a.as<JsonObjectConst>(),"current"));
 auto outcome=httpOutcome(404,"");assert(outcome.transportOk&&!outcome.httpOk&&!outcome.ok&&!strcmp(outcome.error,"http_status_failed"));
 outcome=httpOutcome(200,"response_read_failed");assert(!outcome.transportOk&&outcome.httpOk&&!outcome.ok);
 outcome=httpOutcome(204,"");assert(outcome.transportOk&&outcome.httpOk&&outcome.ok);
 outcome=httpOutcome(0,"");assert(!outcome.transportOk&&!outcome.ok);
 assert(httpStatusSuccessful(200)&&httpStatusSuccessful(204));assert(!httpStatusSuccessful(301)&&!httpStatusSuccessful(404)&&!httpStatusSuccessful(-1));
 std::cout<<"Nonmutating discovery, explicit preparation, restore parity, scoped policy, leases and request validation passed\n";
}
