#include "../FileService.h"
#include "../WireBytes.h"
#include "../Hardware.h"
#include "../TimeSync.h"
#include <esp_sntp.h>
#include <sys/time.h>
#include "../BoardPackage.h"
#include "../Core.h"
#include "drivers/esp32-services/EspMemory.h"
#include "../RpcEnvelope.h"
#include "../ScriptHost.h"
#include "../Workers.h"
#include "../SystemFiles.h"
#include "drivers/esp32-services/LittleFsProvider.h"
#include "../WifiManager.h"
#include "../ConnectionProfiles.h"
#include "../NfsService.h"
#include "../SocketService.h"
#include "../HttpApplications.h"
#include "../RemoteRequests.h"
#include "../DriverRequests.h"
#include <transports/SocketTransport.h>
#include <WiFi.h>
#include <Network.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_partition.h>
#include <esp_littlefs.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WebSocketsClient.h>
#include <atomic>
namespace notesos {
static Core core;static const char*resourceStartupError=nullptr;static Vfs vfs;static Preferences preferences;static WebServer http(80);static DNSServer dns;
static char apSsid[40],bootId[17];static bool mounted=false;static uint32_t cursor=0;
static void* allocateRAM(size_t n){return core.resources().allocate(n,ResourceBudget::Region::Internal);}
static void freeMemoryFile(void*p){core.resources().freeAllocation(p);}
static MemoryFiles ramFiles("ram","/storage/ram","ram",true,allocateRAM,freeMemoryFile);static SystemFiles systemFiles;static LittleFsProvider flashFiles(LittleFS,mounted,"/nfs-test");
static NfsService nfsService(vfs,core.resources());static esp32_nfs::SocketTransport nfsTransport;static SocketService sockets(core);static HttpApplications routes(core);static RemoteRequests remoteRequests(core);static ConnectionProfiles connections;
static uint64_t uptime(){return esp_timer_get_time()/1000;}
static bool networkActive();static bool webSocketActive();static bool applicationNetworkActive(){return sockets.active()||routes.active()||webSocketActive();}
static void event(const char*p,const char*name,const char*value=nullptr){JsonDocument d;if(value)d["value"]=value;core.emit(p,name,d.as<JsonObjectConst>());}
static void emit(const char*p,const char*name,JsonObjectConst d,uint32_t targetOwner=0){core.emit(p,name,d,targetOwner,uptime());}

struct Receipt{uint32_t id=0;bool complete=false,ok=false;char state[48]{},error[80]{};};static Receipt jobs[8];static uint32_t nextJob=1;
static void rememberJob(uint32_t id){jobs[id%8]={};jobs[id%8].id=id;}
#include "drivers/esp32-services/EspTimeService.h"
#include "drivers/esp32-services/WifiService.h"
#include "drivers/esp32-services/NetworkService.h"
#include "drivers/esp32-services/WebSocketService.h"
#include "../ScriptService.h"
#include "../WorkerService.h"
static FileService fileService(vfs);
static const char*files(const char*m,JsonObjectConst a,JsonObject o){return fileService.invoke(m,a,o);}

static const char*nfs(const char*m,JsonObjectConst a,JsonObject out){if(!strcmp(m,"start")){NfsOptions options;strcpy(options.provider,"flash");JsonDocument parsed;parsed["provider"]="flash";parsed["export_path"]="/flash";for(auto f:a)parsed[f.key()]=f.value();if(auto e=NfsOptions::parse(parsed.as<JsonObjectConst>(),options))return e;if(auto e=nfsService.start(options,nfsTransport,esp_random()+uint64_t(1)))return e;}
 else if(!strcmp(m,"stop")){if(auto e=nfsService.stop())return e;}else if(!strcmp(m,"memory"))return nfsService.memory(a,out);else if(strcmp(m,"read"))return "unsupported_method";nfsService.read(out);return nullptr;}
static void respond(JsonObjectConst request,JsonObject reply){invokeEnvelope(core,request,reply,bootId,uptime());}
void begin(){
 Serial.setRxBufferSize(4096);Serial.begin(115200);preferences.begin("agent-os",false);snprintf(bootId,sizeof bootId,"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
 vfs.add(ramFiles);vfs.add(systemFiles);vfs.add(flashFiles);resourceStartupError=configureEspMemory(core.resources());core.setEventObserver(scriptEvent);
 core.onOwnerRelease(&nextJob,[](void*,uint32_t owner){if(networkJob&&networkJob->owner==owner){networkJob->cancel=true;networkJob->orphan=true;}if(wsOwner==owner)stopWebSocket();});
 Device devices[]={
 {"/sys/capabilities","list,describe","resource discovery",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"describe"))return core.describe(a["path"]|"",o);unsigned start=a["cursor"]|0u,limit=a["limit"]|4u;if(!limit||limit>48)return "limit";core.list(o["devices"].to<JsonArray>(),start,limit,a["details"]|false);o["next_cursor"]=start+o["devices"].size();o["more"]=start+o["devices"].size()<core.deviceCount();return nullptr;}},
 {"/sys/board","read","native board identity",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["board"]=boardpack::ID;o["mcu"]=boardpack::MCU;o["package_fingerprint"]=boardpack::FINGERPRINT;o["native_drivers"]=boardpack::DRIVERS;o["platform"]=boardpack::PLATFORM_ID;o["hardware_composition"]=boardpack::COMPOSITION_ID;o["initial_runtime"]=boardpack::RUNTIME_ID;o["mac"]=WiFi.macAddress();o["cores"]=1;return nullptr;}},
 {"/sys/stats","read","resource telemetry",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["heap_free_bytes"]=ESP.getFreeHeap();o["largest_free_block_bytes"]=ESP.getMaxAllocHeap();o["minimum_free_heap_bytes"]=ESP.getMinFreeHeap();o["uptime_ms"]=uptime();o["stack_words"]=uxTaskGetStackHighWaterMark(nullptr);return nullptr;}},
 {"/sys/resources","read,configure","shared reservations",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure")){if(core.callerOwner())return "system_policy_required";if(auto e=core.resources().configure(a))return e;}core.resources().read(o);return nullptr;}},
 {"/sys/connections","read,configure","application service endpoints",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{return connections.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("connections",s)==n;});}},
 {"/sys/operations","request,read,release","retained asynchronous completion observations",[](const char*m,JsonObjectConst a,JsonObject o){return core.operation(m,a,o);}},
 {"/sys/runtime","read,load,restart,stop,configure","dynamic Lua application",scriptHandler},
 {"/sys/workers","read,submit,cancel,release","isolated FreeRTOS workers",workerHandler,workerAsyncDescription},
 {"/sys/timers","read,write,remove","scheduled events",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return core.timer(a,o);if(!strcmp(m,"remove"))return core.removeTimer(a["id"]|0u)?nullptr:"not_found";core.listTimers(o["timers"].to<JsonArray>());return nullptr;}},
 {"/sys/rules","read,write,remove","event rules",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return core.setRule(a,o);if(!strcmp(m,"remove"))return core.removeRule(a["id"]|0u)?nullptr:"not_found";core.listRules(o["rules"].to<JsonArray>());return nullptr;}},
 {"/sys/events","read","event stream",[](const char*,JsonObjectConst a,JsonObject o)->const char*{core.events(a["after"]|0u,a["limit"]|8u,o);return nullptr;}},
 {"/dev/wifi","read,describe,configure,write,profiles,profile_set,profile_remove,connect,resume_auto,disconnect,off,scan_start,scan_results,scan_clear","Wi-Fi configuration",wifiHandler},
 {"/net/default","read,configure,connect","default network",wifiHandler},
 {"/net/http","read,request,receive,cancel,release","HTTP client",networkHandler,httpAsyncDescription},
 {"/net/ws","read,connect,send,receive,close","WS/WSS transport",wsHandler,wsAsyncDescription},
 {"/net/tcp","read,status,listen,connect,write,close","TCP transport",[](const char*m,JsonObjectConst a,JsonObject o){return sockets.invoke(m,a,o);}},
 {"/net/http-server","read,route,respond,release,stop","application routes",[](const char*m,JsonObjectConst a,JsonObject o){return routes.invoke(m,a,o);}},
 {"/services/remote","read,submit,peek,complete,release","remote services",[](const char*m,JsonObjectConst a,JsonObject o){return remoteRequests.invoke(m,a,o,uptime(),bootId);},remoteAsyncDescription},
 {"/services/nfs","read,start,stop,memory","on-demand NFS",nfs},
 {"/dev/flash","read,mount,format","internal LittleFS volume",[](const char*m,JsonObjectConst a,JsonObject o){return core.invoke("/sys/storage",m,a,o,core.callerOwner());},nullptr,"read"},
 {"/sys/storage","read,mount,format","storage providers",[](const char*m,JsonObjectConst,JsonObject o)->const char*{if(!strcmp(m,"format")){if(core.callerOwner())return "system_scope_required";if(nfsService.active()||vfs.hasLeasesUnder("/storage/flash"))return "storage_in_use";LittleFS.end();mounted=false;auto error=esp_littlefs_format("nfs_test");if(error!=ESP_OK){o["sdk_error"]=int(error);return "format_failed";}}if((!strcmp(m,"mount")||!strcmp(m,"format"))&&!mounted){mounted=LittleFS.begin(false,"/nfs-test",4,"nfs_test");if(!mounted){auto p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"nfs_test");o["partition_found"]=p!=nullptr;if(p){o["offset"]=p->address;o["size"]=p->size;}return "mount_failed";}}vfs.describe(o["providers"].to<JsonArray>());return nullptr;}},
 {"/fs",FileService::Methods,"VFS files",files},
 {"/services/time","read,describe,configure,sync","asynchronous NTP",timeHandler},
 {"/sys/clock","read,write","wall and monotonic clock",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write")){ClockChange c;if(auto e=validateClockChange(a,c))return e;if(c.cpu)return "use_power_adapter";if(c.wall){timeval t{time_t(c.unixSeconds),0};if(settimeofday(&t,nullptr))return "clock_failed";timeWasSet=true;}}o["monotonic_ms"]=uptime();o["unix_s"]=int64_t(time(nullptr));o["wall_clock_valid"]=timeWasSet.load()||time(nullptr)>1700000000;return nullptr;}},
 {"/dev/usb","read","USB CDC",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["connected"]=bool(Serial);return nullptr;}}
 };
  const struct {const char*path;const char*methods;} reads[]={{"/dev/usb","read"},{"/dev/wifi","read,describe,profiles,scan_results"},{"/fs","mounts,stat,list,read"},{"/net/default","read,describe,profiles,scan_results"},{"/net/http","read"},{"/net/http-server","read"},{"/net/tcp","status"},{"/net/ws","read"},{"/services/nfs","read"},{"/services/remote","read,peek"},{"/services/time","read,describe"},{"/sys/board","read"},{"/sys/capabilities","list,describe"},{"/sys/clock","read"},{"/sys/events","read"},{"/sys/operations","read"},{"/sys/resources","read"},{"/sys/rules","read"},{"/sys/runtime","read"},{"/sys/stats","read"},{"/sys/storage","read,list"},{"/sys/timers","read"},{"/sys/workers","read"}};
for(auto d:devices){for(auto r:reads)if(!strcmp(d.path,r.path))d.readMethods=r.methods;core.add(d);}
 connections.restore(preferences.getString("connections","{}").c_str());Network.begin();uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);snprintf(apSsid,sizeof apSsid,"Agentic-Notes-%02X%02X%02X",mac[3],mac[4],mac[5]);restoreWifi();core.clock(uptime());wifiManager.poll(uptime());restoreRuntime();timeService.restore(preferences.getString("time_sync","{}").c_str());
 http.on("/",HTTP_GET,[]{JsonDocument r;if(routes.handle("/","GET","",uptime(),r.to<JsonObject>())){http.send(r["status"]|200,r["content_type"]|"text/plain",r["body"]|"");return;}for(auto a:boardpack::ASSETS)if(!strcmp(a.path,"/setup.html")){http.send(200,"text/html; charset=utf-8",a.data);return;}http.send(404,"text/plain","UI package missing");});
 http.on("/os/invoke",HTTP_POST,[]{JsonDocument request,reply;if(http.arg("plain").length()>4096||deserializeJson(request,http.arg("plain"))){http.send(400,"application/json","{\"ok\":false,\"error\":\"invalid_json\"}");return;}respond(request.as<JsonObjectConst>(),reply.to<JsonObject>());String data;serializeJson(reply,data);http.send(200,"application/json",data);});
 http.onNotFound([]{JsonDocument r;if(routes.handle(http.uri().c_str(),http.method()==HTTP_GET?"GET":http.method()==HTTP_POST?"POST":http.method()==HTTP_PUT?"PUT":http.method()==HTTP_DELETE?"DELETE":"OTHER",http.arg("plain").c_str(),uptime(),r.to<JsonObject>())){http.send(r["status"]|200,r["content_type"]|"text/plain",r["body"]|"");return;}http.sendHeader("Location","/",true);http.send(302,"text/plain","");});http.begin();event("/sys/board","boot");
}
void poll(){core.clock(uptime());boardWifi.service();wifiManager.poll(uptime());scriptHost.tick(uptime());workers.poll();timeService.poll(uptime(),WiFi.status()==WL_CONNECTED);pollNetwork();pollWebSocket();sockets.poll();routes.tick(uptime());remoteRequests.tick(uptime());nfsService.poll(millis());dns.processNextRequest();http.handleClient();
 static char line[4097];static size_t used=0;static bool overflow=false;
 for(int n=0;n<512&&Serial.available();++n){char ch=Serial.read();if(ch=='\n'){if(used&&!overflow){line[used]=0;JsonDocument a,r;if(deserializeJson(a,line)){r["ok"]=false;r["error"]="invalid_json";}else respond(a.as<JsonObjectConst>(),r.to<JsonObject>());serializeJson(r,Serial);Serial.println();}used=0;overflow=false;}else if(ch!='\r'){if(used<4096&&!overflow)line[used++]=ch;else overflow=true;}}
 core.tick();core.dispatch(cursor);delay(5);
}
}
