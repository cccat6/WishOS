#include "../Hardware.h"
#include "../BoardPackage.h"
#include "../Core.h"
#include "../ScriptHost.h"
#include "../Workers.h"
#include "../SystemFiles.h"
#include "../LittleFsProvider.h"
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
#include <atomic>
namespace notesos {
static Core core;static Vfs vfs;static Preferences preferences;static WebServer http(80);static DNSServer dns;
static char apSsid[40],bootId[17];static bool mounted=false;static uint32_t cursor=0;
static MemoryFiles ramFiles("ram","/storage/ram","ram");static SystemFiles systemFiles;static LittleFsProvider flashFiles(LittleFS,mounted);
static NfsService nfsService(vfs,[]()->size_t{return core.resources().allocatableHeap();});static esp32_nfs::SocketTransport nfsTransport;static SocketService sockets(core);static HttpApplications routes(core);static RemoteRequests remoteRequests(core);static ConnectionProfiles connections;
static uint64_t uptime(){return esp_timer_get_time()/1000;}
static bool networkActive();static bool applicationNetworkActive(){return sockets.active()||routes.active();}
static void event(const char*p,const char*name,const char*value=nullptr){JsonDocument d;if(value)d["value"]=value;core.emit(p,name,d.as<JsonObjectConst>());}
static void emit(const char*p,const char*name,JsonObjectConst d){core.emit(p,name,d);}
static void hex(const uint8_t*p,size_t n,char*out){for(size_t i=0;i<n;++i)sprintf(out+i*2,"%02x",p[i]);out[n*2]=0;}
struct Receipt{uint32_t id=0;bool complete=false,ok=false;char state[48]{},error[80]{};};static Receipt jobs[8];static uint32_t nextJob=1;
static void rememberJob(uint32_t id){jobs[id%8]={};jobs[id%8].id=id;}
#include "../WifiService.h"
#include "../NetworkService.h"
#include "../ScriptService.h"
#include "../WorkerService.h"
static const char*files(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"mounts")){vfs.describe(out["providers"].to<JsonArray>());return nullptr;}
 char full[256];if(!normalizePath("/",a["file"]|(a["path"]|"/storage/ram"),full,sizeof full))return "invalid_path";const char*rel;auto p=vfs.resolve(full,rel);if(!p)return "unknown_provider";if(!p->available())return "provider_unavailable";
 if(!strcmp(m,"stat")){FileInfo i;if(auto e=p->stat(rel,i))return e;sizeText(out,"size",i.size);out["directory"]=i.directory;return nullptr;}
 if(!strcmp(m,"list")){auto limit=a["limit"]|12u;if(!limit||limit>24)return "list_limit";return p->list(rel,a["cursor"]|0u,limit,out);}
 uint64_t at;if(!parseOffset(a["offset"],at))return "invalid_offset";uint8_t data[1024];size_t n=0;
 if(!strcmp(m,"read")){unsigned length=a["length"]|1024u;if(!length||length>1024)return "chunk_limit";if(auto e=p->read(rel,at,data,length,n))return e;char encoded[2049];hex(data,n,encoded);out["hex"]=encoded;sizeText(out,"next_offset",at+n);return nullptr;}
 if(!p->writable)return "read_only";if(!vfs.canMutate(full))return "file_in_use";
 if(!strcmp(m,"write")){if(a["text"].is<const char*>()){const char*s=a["text"];n=strlen(s);if(n>1024)return "chunk_limit";memcpy(data,s,n);}else{const char*s=a["hex"]|"";size_t z=strlen(s);if(z%2||z>2048)return "chunk_limit";n=z/2;for(size_t i=0;i<n;++i){char b[3]={s[2*i],s[2*i+1],0};if(!isxdigit(b[0])||!isxdigit(b[1]))return "invalid_hex";data[i]=strtoul(b,nullptr,16);}}size_t wrote;if(auto e=p->write(rel,at,data,n,a["truncate"]|false,wrote))return e;out["written"]=wrote;return nullptr;}
 if(!strcmp(m,"mkdir"))return p->mkdir(rel);if(!strcmp(m,"remove"))return p->remove(rel);return "unsupported_method";
}
static const char*nfs(const char*m,JsonObjectConst a,JsonObject out){if(!strcmp(m,"start")){NfsOptions options;strcpy(options.provider,"flash");JsonDocument parsed;parsed["provider"]="flash";parsed["export_path"]="/flash";for(auto f:a)parsed[f.key()]=f.value();if(auto e=NfsOptions::parse(parsed.as<JsonObjectConst>(),options))return e;if(auto e=nfsService.start(options,nfsTransport,esp_random()+uint64_t(1)))return e;}
 else if(!strcmp(m,"stop")){if(auto e=nfsService.stop())return e;}else if(!strcmp(m,"memory"))return nfsService.memory(a,out);else if(strcmp(m,"read"))return "unsupported_method";nfsService.read(out);return nullptr;}
static void respond(JsonObjectConst request,JsonObject reply){JsonDocument result;const char*error=core.invoke(request["path"]|"",request["method"]|"read",request["args"].as<JsonObjectConst>(),result.to<JsonObject>());reply["id"]=request["id"];reply["boot_id"]=bootId;reply["ok"]=!error;if(error)reply["error"]=error;reply["result"]=result;}
void begin(){
 Serial.setRxBufferSize(4096);Serial.begin(115200);preferences.begin("agent-os",false);snprintf(bootId,sizeof bootId,"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
 vfs.add(ramFiles);vfs.add(systemFiles);vfs.add(flashFiles);core.resources().heapProbe([]()->size_t{return ESP.getFreeHeap();});core.setEventObserver(scriptEvent);
 core.onOwnerRelease(&nextJob,[](void*,uint32_t owner){if(networkJob&&networkJob->owner==owner){networkJob->cancel=true;networkJob->orphan=true;}});
 Device devices[]={
 {"/sys/capabilities","list,describe","resource discovery",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"describe"))return core.describe(a["path"]|"",o);unsigned start=a["cursor"]|0u,limit=a["limit"]|4u;if(!limit||limit>48)return "limit";core.list(o["devices"].to<JsonArray>(),start,limit,a["details"]|false);o["next_cursor"]=start+o["devices"].size();o["more"]=start+o["devices"].size()<core.deviceCount();return nullptr;}},
 {"/sys/board","read","native board identity",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["board"]=boardpack::ID;o["mcu"]="ESP32-C3";o["mac"]=WiFi.macAddress();o["cores"]=1;return nullptr;}},
 {"/sys/stats","read","resource telemetry",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["heap_free_bytes"]=ESP.getFreeHeap();o["largest_free_block_bytes"]=ESP.getMaxAllocHeap();o["minimum_free_heap_bytes"]=ESP.getMinFreeHeap();o["uptime_ms"]=uptime();o["stack_words"]=uxTaskGetStackHighWaterMark(nullptr);return nullptr;}},
 {"/sys/resources","read,configure","shared reservations",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure")){if(auto e=core.resources().configure(a))return e;}core.resources().read(o);return nullptr;}},
 {"/sys/connections","read,configure","application service endpoints",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{return connections.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("connections",s)==n;});}},
 {"/sys/runtime","read,load,restart,stop,configure","dynamic Lua application",scriptHandler},
 {"/sys/workers","read,submit,cancel,release","isolated FreeRTOS workers",workerHandler},
 {"/sys/timers","read,write,remove","scheduled events",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return core.timer(a,o);if(!strcmp(m,"remove"))return core.removeTimer(a["id"]|0u)?nullptr:"not_found";core.listTimers(o["timers"].to<JsonArray>());return nullptr;}},
 {"/sys/rules","read,write,remove","event rules",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return core.setRule(a,o);if(!strcmp(m,"remove"))return core.removeRule(a["id"]|0u)?nullptr:"not_found";core.listRules(o["rules"].to<JsonArray>());return nullptr;}},
 {"/sys/events","read","event stream",[](const char*,JsonObjectConst a,JsonObject o)->const char*{core.events(a["after"]|0u,a["limit"]|8u,o);return nullptr;}},
 {"/dev/wifi","read,describe,configure,write,profiles,profile_set,profile_remove,connect,resume_auto,disconnect,off,scan_start,scan_results,scan_clear","Wi-Fi configuration",wifiHandler},
 {"/net/default","read,configure,connect","default network",wifiHandler},
 {"/net/http","read,request,receive,cancel,release","HTTP client",networkHandler},
 {"/net/tcp","read,listen,connect,write,close","TCP transport",[](const char*m,JsonObjectConst a,JsonObject o){return sockets.invoke(m,a,o);}},
 {"/net/http-server","read,route,respond,release,stop","application routes",[](const char*m,JsonObjectConst a,JsonObject o){return routes.invoke(m,a,o);}},
 {"/services/remote","read,submit,peek,complete,release","remote services",[](const char*m,JsonObjectConst a,JsonObject o){return remoteRequests.invoke(m,a,o,uptime(),bootId);}},
 {"/services/nfs","read,start,stop,memory","on-demand NFS",nfs},
 {"/sys/storage","read,mount,format","storage providers",[](const char*m,JsonObjectConst,JsonObject o)->const char*{if(!strcmp(m,"format")){if(core.callerOwner())return "system_scope_required";if(nfsService.active()||vfs.hasLeasesUnder("/storage/flash"))return "storage_in_use";LittleFS.end();mounted=false;auto error=esp_littlefs_format("nfs_test");if(error!=ESP_OK){o["sdk_error"]=int(error);return "format_failed";}}if((!strcmp(m,"mount")||!strcmp(m,"format"))&&!mounted){mounted=LittleFS.begin(false,"/nfs-test",4,"nfs_test");if(!mounted){auto p=esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_DATA_SPIFFS,"nfs_test");o["partition_found"]=p!=nullptr;if(p){o["offset"]=p->address;o["size"]=p->size;}return "mount_failed";}}vfs.describe(o["providers"].to<JsonArray>());return nullptr;}},
 {"/fs","mounts,stat,list,read,write,mkdir,remove","VFS files",files},
 {"/dev/usb","read","USB CDC",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["connected"]=bool(Serial);return nullptr;}}
 };for(auto d:devices)core.add(d);
 connections.restore(preferences.getString("connections","{}").c_str());Network.begin();uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);snprintf(apSsid,sizeof apSsid,"Agentic-Notes-%02X%02X%02X",mac[3],mac[4],mac[5]);restoreWifi();core.clock(uptime());wifiManager.poll(uptime());restoreRuntime();
 http.on("/",HTTP_GET,[]{for(auto a:boardpack::ASSETS)if(!strcmp(a.path,"/setup.html")){http.send(200,"text/html; charset=utf-8",a.data);return;}http.send(404,"text/plain","UI package missing");});
 http.on("/os/invoke",HTTP_POST,[]{JsonDocument request,reply;if(http.arg("plain").length()>4096||deserializeJson(request,http.arg("plain"))){http.send(400,"application/json","{\"ok\":false,\"error\":\"invalid_json\"}");return;}respond(request.as<JsonObjectConst>(),reply.to<JsonObject>());String data;serializeJson(reply,data);http.send(200,"application/json",data);});
 http.onNotFound([]{JsonDocument r;if(routes.handle(http.uri().c_str(),http.method()==HTTP_GET?"GET":"POST",http.arg("plain").c_str(),uptime(),r.to<JsonObject>())){http.send(r["status"]|200,r["content_type"]|"text/plain",r["body"]|"");return;}http.sendHeader("Location","/",true);http.send(302,"text/plain","");});http.begin();event("/sys/board","boot");
}
void poll(){core.clock(uptime());boardWifi.service();wifiManager.poll(uptime());scriptHost.tick(uptime());workers.poll();pollNetwork();sockets.poll();routes.tick(uptime());remoteRequests.tick(uptime());nfsService.poll(millis());dns.processNextRequest();http.handleClient();
 static char line[4097];static size_t used=0;static bool overflow=false;
 for(int n=0;n<512&&Serial.available();++n){char ch=Serial.read();if(ch=='\n'){if(used&&!overflow){line[used]=0;JsonDocument a,r;if(deserializeJson(a,line)){r["ok"]=false;r["error"]="invalid_json";}else respond(a.as<JsonObjectConst>(),r.to<JsonObject>());serializeJson(r,Serial);Serial.println();}used=0;overflow=false;}else if(ch!='\r'){if(used<4096&&!overflow)line[used++]=ch;else overflow=true;}}
 core.tick();core.dispatch(cursor);delay(5);
}
}
