#include "ConnectionProfiles.h"
#include "drivers/esp32-services/EspWifiCapabilities.h"
#include "fixtures/composition/Defaults.h"
#include "CachePath.h"
#include "DriverRequests.h"
#include "fixtures/composition/MicRequests.h"
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
 {ConnectionProfiles profiles;JsonDocument a,o;a["profiles"]["custom-reader"]["address"]="arbitrary-protocol://target";a["profiles"]["custom-reader"]["custom"]=-1;
 assert(!profiles.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(o["profiles"]["custom-reader"]["custom"]==-1);auto revision=o["revision"].as<unsigned>();a["profiles"]["invalid/name"]["x"]=1;assert(profiles.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(!profiles.invoke("read",JsonObjectConst(),o.to<JsonObject>(),nullptr)&&o["revision"]==revision&&!o["profiles"].containsKey("invalid/name"));
 assert(!profiles.restore("{\"invalid/name\":{}}"));assert(!profiles.invoke("read",JsonObjectConst(),o.to<JsonObject>(),nullptr)&&o["profiles"]["custom-reader"]["custom"]==-1);}

 // A new sensor can declare signed offsets and fractional thresholds without a
 // kernel edit. Online changes and restored snapshots use the identical schema.
 const Setting sensorSettings[]={
  {"/sensors/custom","offset","integer","0","",-180,180},
  {"/sensors/custom","threshold","number","0.5","",-1.5,2.5},
  {"/sensors/custom","direction","integer","0","-1,0,1",-1,1}
 };
 Policies sensor(sensorSettings,3);JsonDocument sensorOut;
 auto update=json(R"({"changes":[{"resource":"/sensors/custom","settings":{"offset":-12,"threshold":0.125,"direction":-1}}]})");
 assert(!sensor.apply(update.as<JsonObjectConst>(),sensorOut.to<JsonObject>()));
 assert(sensor.get<int>("/sensors/custom","offset",0)==-12&&sensor.get<double>("/sensors/custom","threshold",0)==0.125);
 auto rev=sensor.revision();update["changes"][0]["settings"]["threshold"]=3.0;
 assert(sensor.apply(update.as<JsonObjectConst>(),sensorOut.to<JsonObject>())&&sensor.revision()==rev);
 assert(sensor.get<int>("/sensors/custom","offset",0)==-12);
 assert(!sensor.restore(R"({"system":{"/sensors/custom":{"offset":-180.5}}})"));
 assert(!sensor.restore(R"({"system":{"/sensors/custom":{"threshold":"0.25"}}})"));
 assert(!sensor.restore(R"({"system":{"/sensors/custom":{"threshold":true}}})"));
 assert(sensor.restore(R"({"system":{"/sensors/custom":{"offset":-90,"threshold":-0.5,"direction":1}}})"));
 assert(sensor.get<double>("/sensors/custom","threshold",0)==-0.5);
 sensor.describe(sensorOut.to<JsonArray>());assert(sensorOut[1]["minimum"]==-1.5&&sensorOut[1]["maximum"]==2.5);
 // Device defaults, persisted overrides and app scope preserve inheritance.
 {const Setting fields[]={{"/view","font","string","\"builtin\"","",0,255},{"/view","size","integer","16","",8,96}};
  Policies style(fields,2);assert(style.defaults(R"({"system":{"/view":{"font":"factory.ttf","size":20}}})"));
  assert(style.restore(R"({"system":{"/view":{"size":24}}})"));JsonDocument result;style.resolve("/view","reader",result.to<JsonObject>());
  assert(result["font"]=="factory.ttf"&&result["size"]==24);
  auto change=json(R"({"scope":"reader","changes":[{"resource":"/view","settings":{"font":"reader.ttf"}}]})");assert(!style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>()));
  style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="reader.ttf"&&result["size"]==24);
  style.resolve("/view","other",result.to<JsonObject>());assert(result["font"]=="factory.ttf");
  assert(style.defaults(R"({"system":{"/view":{"font":"updated-default.ttf","size":20}}})"));
  style.resolve("/view","other",result.to<JsonObject>());assert(result["font"]=="updated-default.ttf"&&result["size"]==24);
  assert(!style.defaults(R"({"system":{"/view":{"size":200}}})"));style.resolve("/view","other",result.to<JsonObject>());assert(result["font"]=="updated-default.ttf");
  // "Default" removes the override; it keeps following future system changes.
  change=json(R"({"scope":"reader","changes":[{"resource":"/view","inherit":["font"]}]})");
  auto revision=style.revision();assert(!style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>(),true));assert(style.revision()==revision);
  style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="reader.ttf");
  assert(!style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>()));style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="updated-default.ttf");
  change=json(R"({"changes":[{"resource":"/view","settings":{"font":"user-system.ttf"}}]})");assert(!style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>()));style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="user-system.ttf"&&result["size"]==24);
  change=json(R"({"changes":[{"resource":"/view","inherit":["font","size"]}]})");assert(!style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>()));style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="updated-default.ttf"&&result["size"]==20);
  change=json(R"({"scope":"reader","changes":[{"resource":"/view","inherit":["font"],"settings":{"font":"conflict.ttf"}}]})");revision=style.revision();assert(!strcmp(style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>()),"setting_and_inherit_conflict"));assert(style.revision()==revision);
  change=json(R"({"changes":[{"resource":"/view","settings":{"font":"other.ttf"}},{"resource":"/view","inherit":["missing"]}]})");assert(!strcmp(style.apply(change.as<JsonObjectConst>(),result.to<JsonObject>()),"unknown_or_readonly_setting"));style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="updated-default.ttf");
  style.describe(result.to<JsonArray>());assert(result[0]["default"]=="updated-default.ttf"&&result[0]["inherit"]==true);
 }
 // Policy metadata uses the registered heaps too; PSRAM-capable targets retain
 // internal memory for DMA/task stacks, while a single-heap target still works.
 {ResourceBudget memory;memory.heap(ResourceBudget::Region::External,{[]()->size_t{return 1048576;},[]()->size_t{return 1048576;},[]()->size_t{return 1048576;},[](void*p,size_t n)->void*{if(!n){free(p);return nullptr;}return realloc(p,n);}});
  {const Setting fields[]={{"/view","font","string","\"builtin\"","",0,255},{"/view","size","integer","16","",8,96},{"/view","enabled","boolean","true"},{"/view","fraction","number","0.25","",0,1}};Policies style(fields,4,&memory);assert(style.defaults(R"({"system":{"/view":{"font":"factory.ttf","size":20}}})"));assert(style.restore(R"({"reader":{"/view":{"font":"reader.ttf","size":24}}})"));
   JsonDocument status,result;memory.read(status.to<JsonObject>());assert(status["heaps"][0]["shared_allocated_bytes"]==0&&status["heaps"][1]["shared_allocated_bytes"].as<unsigned>()>0);
   style.resolve("/view","reader",result.to<JsonObject>());assert(result["font"]=="reader.ttf");style.resolve("/view","other",result.to<JsonObject>());assert(result["font"]=="factory.ttf");
   JsonDocument limit;limit["memory_limit_bytes"]=uint32_t(memory.promised());assert(!memory.configure(limit.as<JsonObjectConst>()));
   assert(style.get<unsigned>("/view","size",0,"reader")==24&&style.get<unsigned>("/view","size",0)==20);
   assert(style.get<bool>("/view","enabled",false)&&style.get<double>("/view","fraction",0)==0.25);
  }assert(memory.promised()==0);
 }
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
