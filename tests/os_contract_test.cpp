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
 assert(!p.restore(R"({"app:reader":{"/sys/storage":{"ram_quota_bytes":10000}}})"));
 assert(!p.restore(R"({"bad/scope":{"/sys/input":{"debounce_ms":10}}})"));
 assert(!p.restore(R"({"system":{"/sys/bus":{"display_spi_hz":24000000}}})"));
 assert(p.get<uint32_t>("/sys/bus","display_spi_hz",0)==16000000);
 auto a=json(R"({"changes":[{"resource":"/sys/input","settings":{"debounce_ms":10}},{"resource":"/sys/bus","settings":{"display_spi_hz":24000000}}]})");
 assert(!strcmp(p.apply(a.as<JsonObjectConst>(),out.to<JsonObject>()),"platform_clock_limit"));
 assert(p.revision()==original&&p.get<uint32_t>("/sys/input","debounce_ms",0)==25);
 assert(p.restore(R"({"system":{"/sys/input":{"debounce_ms":10}}})"));
 assert(p.get<uint32_t>("/sys/input","debounce_ms",0)==10);
 // Different resources must not leak fields from the previous resolution.
 p.resolve("/sys/input","system",out.to<JsonObject>());p.resolve("/sys/bus","system",out.as<JsonObject>());
 assert(out["debounce_ms"].isNull());
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
 a=json(R"({"cpu_mhz":80,"unix_s":-1})");assert(validateClockChange(a.as<JsonObjectConst>(),clock));
 a=json(R"({"cpu_mhz":80,"unknown":1})");assert(validateClockChange(a.as<JsonObjectConst>(),clock));
 auto outcome=httpOutcome(404,"");assert(outcome.transportOk&&!outcome.httpOk&&!outcome.ok&&!strcmp(outcome.error,"http_status_failed"));
 outcome=httpOutcome(200,"response_read_failed");assert(!outcome.transportOk&&outcome.httpOk&&!outcome.ok);
 outcome=httpOutcome(204,"");assert(outcome.transportOk&&outcome.httpOk&&outcome.ok);
 outcome=httpOutcome(0,"");assert(!outcome.transportOk&&!outcome.ok);
 assert(httpStatusSuccessful(200)&&httpStatusSuccessful(204));assert(!httpStatusSuccessful(301)&&!httpStatusSuccessful(404)&&!httpStatusSuccessful(-1));
 std::cout<<"Nonmutating discovery, explicit preparation, restore parity, scoped policy, leases and request validation passed\n";
}
