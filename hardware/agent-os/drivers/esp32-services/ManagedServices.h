// Native composition integration; portable mechanisms do not know ESP32 pins.
static Diagnostics diagnostics(core);
static AssetTransfers transfers(core,vfs,bootId);
static AgentService agentService(core,transfers,vfs);
class EspTelemetry:public TelemetryBackend {
 std::atomic<int>state_{0};std::atomic<bool>cancel_{false};char url_[256]{},body_[768]{},error_[64]{};uint32_t reservation_=0;ProxySettings proxy_;
 static void run(void*p){auto&s=*static_cast<EspTelemetry*>(p);int status=-1;{
  std::unique_ptr<ProxySecureClient> transport(new(std::nothrow)ProxySecureClient(s.proxy_));if(!transport){copy(s.error_,sizeof s.error_,"telemetry_client_memory");s.state_=-1;vTaskDelete(nullptr);return;}transport->usePlatformRoots();HTTPClient client;client.setTimeout(10000);client.setConnectTimeout(10000);client.setReuse(false);copy(s.error_,sizeof s.error_,"telemetry_cancelled");
  if(!s.cancel_&&client.begin(*transport,s.url_)){client.addHeader("Content-Type","application/json");int code=client.POST((uint8_t*)s.body_,strlen(s.body_));snprintf(s.error_,sizeof s.error_,"telemetry_http_%d",code);
   if(code>=200&&code<300){struct Sink:Stream{char bytes[256]{};size_t size=0;int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}size_t write(uint8_t b)override{return write(&b,1);}size_t write(const uint8_t*p,size_t n)override{if(n>=sizeof bytes-size)return 0;memcpy(bytes+size,p,n);size+=n;return n;}}sink;
    if(client.writeToStream(&sink)>=0){JsonDocument reply,sent;if(!deserializeJson(reply,sink.bytes,sink.size)&&!deserializeJson(sent,s.body_)&&reply["accepted"]==true&&reply["receipt"]==sent["receipt"]){status=1;s.error_[0]=0;}else copy(s.error_,sizeof s.error_,"telemetry_receipt_invalid");}}
   client.end();}
 }s.state_=status;vTaskDelete(nullptr);}

public:
 const char*start(const char*url,const char*json)override{if(state_!=0)return "telemetry_busy";if(!copy(url_,sizeof url_,url)||!copy(body_,sizeof body_,json))return "telemetry_size";reservation_=core.resources().acquire(65000,0);if(!reservation_)return "telemetry_memory_budget";core.resources().used(reservation_,sizeof(*this)+6144);proxy_=networkProxy.snapshot();cancel_=false;state_=2;if(xTaskCreate(run,"wish-telemetry",6144,this,1,nullptr)!=pdPASS){state_=0;core.resources().release(reservation_);reservation_=0;return "task_allocation_failed";}return nullptr;}
 int poll()override{if(state_==2)return 0;int value=state_.exchange(0);if(reservation_){core.resources().release(reservation_);reservation_=0;}return value;}
 void cancel()override{cancel_=true;}
 const char*error()const override{return error_;}
 bool active()const override{return state_==2;}
};
static EspTelemetry telemetryBackend;
static uint64_t telemetryRandom(){return uint64_t(esp_random())<<32|esp_random();}
static Telemetry telemetry(core,diagnostics,telemetryBackend,"",bootId,telemetryRandom);
static const char*proxyHandler(const char*m,JsonObjectConst a,JsonObject o){return networkProxy.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("proxy",s)==n;});}
static const char*logHandler(const char*m,JsonObjectConst a,JsonObject o){return diagnostics.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("logs",s)==n;});}
static const char*telemetryHandler(const char*m,JsonObjectConst a,JsonObject o){return telemetry.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("telemetry",s)==n;});}
static const char*transferHandler(const char*m,JsonObjectConst a,JsonObject o){return transfers.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("downloads",s)==n;});}
static const char*agentHandler(const char*m,JsonObjectConst a,JsonObject o){return agentService.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("ai",s)==n;});}
static void registerManagedServices(){
 transfers.identity(bootId);
 core.add({"/net/proxy","read,configure","outbound proxy policy",proxyHandler,[](JsonObject o){networkProxy.describe(o);},"read"});
 core.add({"/sys/logs","read,write,configure","structured bounded diagnostics",logHandler,[](JsonObject o){diagnostics.describe(o);},"read"});
 core.add({"/sys/telemetry","read,configure,flush","optional registration and aggregate diagnostics",telemetryHandler,[](JsonObject o){telemetry.describe(o);},"read"});
 core.add({"/sys/transfers","read,configure,begin,file,copy_file,chunk,close_file,commit,abort,release","verified streamed resources",transferHandler,[](JsonObject o){transfers.describe(o);},"read"});
 core.add({"/services/ai",AgentService::Methods,"managed AI tasks and result application",agentHandler,[](JsonObject o){agentService.describe(o);},"read,read_action,read_result"});
 core.diagnostics(&diagnostics,[](void*p,const char*s,const char*e,JsonObjectConst d,uint32_t owner){static_cast<Diagnostics*>(p)->event(s,e,d,owner);},[](void*p,const char*s,const char*m,const char*e,uint32_t owner){static_cast<Diagnostics*>(p)->call(s,m,e,owner);});
 JsonDocument config,logs;
 for(auto a:boardpack::ASSETS)if(!strcmp(a.path,"/services.json")){deserializeJson(config,a.data,a.size);break;}
 // Package startup policy is applied once through the same validated storage
 // interface; replacing an ordinary app must not reset system volume quotas.
 for(auto settings:config["volatile_storage"].as<JsonArrayConst>()){JsonDocument result;if(auto e=core.invoke("/sys/storage","configure",settings,result.to<JsonObject>())){JsonDocument data;data["error"]=e;core.emit("/sys/storage","configuration_failed",data.as<JsonObjectConst>());}}
 if(config["logs"].is<JsonObjectConst>())logs["settings"]=config["logs"];if(logs["settings"].isNull())logs["settings"].to<JsonObject>();JsonDocument out;diagnostics.invoke("configure",logs.as<JsonObjectConst>(),out.to<JsonObject>());
 auto saved=preferences.getString("logs","");if(saved.length()){JsonDocument a,b;if(!deserializeJson(b,saved)){a["settings"]=b;diagnostics.invoke("configure",a.as<JsonObjectConst>(),out.to<JsonObject>());}}
 saved=preferences.getString("downloads","");if(!saved.length()&&config["downloads"].is<JsonObjectConst>())serializeJson(config["downloads"],saved);if(saved.length())transfers.restore(saved.c_str());
 saved=preferences.getString("proxy","");if(saved.length())networkProxy.restore(saved.c_str());
 saved=preferences.getString("ai","");if(saved.length())agentService.restore(saved.c_str());
 saved=preferences.getString("telemetry","");if(!saved.length()&&config["telemetry"].is<JsonObjectConst>())serializeJson(config["telemetry"],saved);if(saved.length())telemetry.restore(saved.c_str());
 uint8_t mac[6];esp_read_mac(mac,ESP_MAC_WIFI_STA);char id[18];snprintf(id,sizeof id,"%02X:%02X:%02X:%02X:%02X:%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);telemetry.identity(id,bootId);switch(esp_reset_reason()){case ESP_RST_POWERON:telemetry.resetReason("power_on");break;case ESP_RST_SW:telemetry.resetReason("software");break;case ESP_RST_PANIC:telemetry.resetReason("panic");break;case ESP_RST_INT_WDT:case ESP_RST_TASK_WDT:case ESP_RST_WDT:telemetry.resetReason("watchdog");break;case ESP_RST_DEEPSLEEP:telemetry.resetReason("deep_sleep");break;case ESP_RST_BROWNOUT:telemetry.resetReason("brownout");break;case ESP_RST_EXT:telemetry.resetReason("external");break;default:telemetry.resetReason("unknown");}setenv("TZ",timeService.config().timezone,1);tzset();
}
static void pollManagedServices(bool suspended=false){diagnostics.poll(uptime());if(!suspended){transfers.poll();agentService.poll();}else telemetryBackend.cancel();telemetry.poll(uptime(),!suspended&&WiFi.status()==WL_CONNECTED);}
