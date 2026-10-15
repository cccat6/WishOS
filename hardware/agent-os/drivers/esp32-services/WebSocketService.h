#ifndef WISHOS_WS_SOURCE
#define WISHOS_WS_SOURCE "/net/ws"
#define WISHOS_WS_DEFAULT_SOURCE
#endif
// The socket client is owned by one background task. The OS loop only exchanges
// bounded messages and observations; DNS/TLS/connect waits never run there.
#ifdef WISHOS_WS_CONTROL
static constexpr unsigned WsIncoming=(MAX_WIRE+1011)/1012;
#else
static constexpr unsigned WsIncoming=4;
#endif
struct WsMessage{uint32_t id=0,generation=0;bool binary=false;size_t size=0;uint8_t data[1024]{};};
struct WsSession {
 WebSocketsClient client;String ca,headers;QueueHandle_t incoming=nullptr,outgoing=nullptr;
 std::atomic<bool>connected{false},stop{false},done{false},readable{false},waitingMemory{false};std::atomic<uint32_t>drops{0},generation{0};
 uint32_t id=0,owner=0,reservation=0;bool firstResult=false,steadyBudget=false;size_t residentBytes=0;WsMessage assembly;bool assembling=false;
 ~WsSession(){client.onEvent([](WStype_t,uint8_t*,size_t){});client.disconnect();if(incoming)vQueueDelete(incoming);if(outgoing)vQueueDelete(outgoing);if(reservation)core.resources().release(reservation);}
};
static WsSession*wsSession=nullptr;static WebSocketsClient*wsClient=nullptr;static uint32_t wsOwner=0,nextWs=1,nextWsMessage=1;
static void wsEvent(WsSession&s,const char*name,uint32_t message=0,size_t bytes=0){
#ifdef WISHOS_WS_CONTROL
 if(!strcmp(name,"sent")||!strcmp(name,"readable"))return;
#endif
 JsonDocument d;d["connection_id"]=s.id;d["owner"]=s.owner;if(message)d["message_id"]=message;d["bytes"]=bytes;emit(WISHOS_WS_SOURCE,name,d.as<JsonObjectConst>(),s.owner);}
static void wsReceive(WsSession&s,WStype_t type,uint8_t*data,size_t n){
 if(type==WStype_CONNECTED||type==WStype_DISCONNECTED){bool linked=type==WStype_CONNECTED;if(linked){++s.generation;core.resources().used(s.reservation,core.resources().reservationLimit(s.reservation));s.steadyBudget=true;}s.connected=linked;if(!linked){s.assembling=false;WsMessage abandoned;while(xQueueReceive(s.incoming,&abandoned,0)==pdTRUE){}while(xQueueReceive(s.outgoing,&abandoned,0)==pdTRUE)wsEvent(s,"send_failed",abandoned.id,abandoned.size);}
  if(!s.firstResult){s.firstResult=true;wsEvent(s,linked?"connected":"failed");}else wsEvent(s,linked?"connected":"disconnected");return;}
 bool initial=type==WStype_FRAGMENT_TEXT_START||type==WStype_FRAGMENT_BIN_START,more=type==WStype_FRAGMENT||type==WStype_FRAGMENT_FIN,whole=type==WStype_TEXT||type==WStype_BIN;
 if(!initial&&!more&&!whole)return;
 if(initial||whole){if(s.assembling)++s.drops;s.assembly={};s.assembly.generation=s.generation.load();s.assembly.binary=type==WStype_BIN||type==WStype_FRAGMENT_BIN_START;s.assembling=true;}
 if(!s.assembling||n>sizeof s.assembly.data-s.assembly.size){s.assembling=false;++s.drops;wsEvent(s,"overflow");return;}
 memcpy(s.assembly.data+s.assembly.size,data,n);s.assembly.size+=n;
 if(whole||type==WStype_FRAGMENT_FIN){s.assembling=false;if(xQueueSend(s.incoming,&s.assembly,0)!=pdTRUE){++s.drops;wsEvent(s,"overflow");}else if(!s.readable.exchange(true))wsEvent(s,"readable");}
}
static void wsTask(void*ptr){auto&s=*static_cast<WsSession*>(ptr);
 while(!s.stop.load()){
  if(!s.connected&&s.steadyBudget){s.client.disconnect();if(!core.resources().rearm(s.reservation,s.residentBytes)){s.waitingMemory=true;vTaskDelay(pdMS_TO_TICKS(100));continue;}s.waitingMemory=false;s.steadyBudget=false;}
  s.client.loop();WsMessage message;
  if(xQueueReceive(s.outgoing,&message,0)==pdTRUE){bool ok=message.generation==s.generation.load()&&s.connected.load()&&!s.stop.load()&&(message.binary?s.client.sendBIN(message.data,message.size):s.client.sendTXT(message.data,message.size));wsEvent(s,ok?"sent":"send_failed",message.id,message.size);}
  vTaskDelay(pdMS_TO_TICKS(2));
 }
 s.client.disconnect();WsMessage message;while(xQueueReceive(s.outgoing,&message,0)==pdTRUE)wsEvent(s,"send_failed",message.id,message.size);
 if(!s.firstResult)wsEvent(s,"failed");s.connected=false;wsEvent(s,"closed");s.done=true;vTaskDelete(nullptr);
}
static bool webSocketActive(){return wsSession!=nullptr;}
static void stopWebSocket(){if(wsSession)wsSession->stop=true;}
static void pollWebSocket(){if(wsSession&&wsSession->done.load()){delete wsSession;wsSession=nullptr;wsClient=nullptr;wsOwner=0;}else if(wsSession&&uxQueueMessagesWaiting(wsSession->incoming)&&!wsSession->readable.exchange(true))wsEvent(*wsSession,"readable");}
static const char*wsHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(strcmp(m,"read")&&strcmp(m,"validate_connect"))pollWebSocket();if(wsSession&&strcmp(m,"validate_connect")&&strcmp(m,"read")&&core.callerOwner()&&core.callerOwner()!=wsOwner)return "ws_owned_by_other";
 const char*fields=(!strcmp(m,"connect")||!strcmp(m,"validate_connect"))?"host,path,ca_pem,tls,verify_peer,port,reconnect_ms,headers":!strcmp(m,"send")?"text,hex":"";
 for(auto f:a)if(!Policies::choice(fields,f.key().c_str()))return "unknown_ws_argument";
 if(!strcmp(m,"close")){stopWebSocket();out["state"]=wsSession?"closing":"closed";return nullptr;}
 if(!strcmp(m,"connect")||!strcmp(m,"validate_connect")){
  if(wsSession&&strcmp(m,"validate_connect"))return "ws_already_open";
  for(auto key:{"host","path","ca_pem"})if(a.containsKey(key)&&(!a[key].is<const char*>()||strlen(a[key].as<const char*>())!=a[key].as<JsonString>().size()))return "ws_string_required";
  for(auto key:{"port","reconnect_ms"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "ws_integer_required";
  if((a.containsKey("tls")&&!a["tls"].is<bool>())||(a.containsKey("verify_peer")&&!a["verify_peer"].is<bool>()))return "ws_boolean_required";
  if(a.containsKey("headers")&&!a["headers"].is<JsonObjectConst>())return "ws_headers_required";
  const char*host=a["host"]|"",*path=a["path"]|"/",*ca=a["ca_pem"]|"";bool tls=a["tls"]|false;uint32_t port=a["port"]|(tls?443u:80u),retry=a["reconnect_ms"]|5000u;
  if(!*host||strlen(host)>253||strpbrk(host,"\r\n")||strpbrk(path,"\r\n")||path[0]!='/'||strlen(path)>512||port<1||port>65535||strlen(ca)>8192||retry<100)return "ws_configuration";
  String extra;for(auto field:a["headers"].as<JsonObjectConst>()){const char*value=field.value().as<const char*>();if(!value||strlen(value)!=field.value().as<JsonString>().size()||strpbrk(field.key().c_str(),"\r\n:")||strpbrk(value,"\r\n"))return "invalid_header";extra+=field.key().c_str();extra+=": ";extra+=value;extra+="\r\n";if(extra.length()>512)return "header_limit";}
  if(!strcmp(m,"validate_connect"))return nullptr;
  auto reservation=core.resources().acquire(tls?70000:28000,core.callerOwner());if(!reservation)return "network_memory_budget";
  auto s=new(std::nothrow)WsSession;if(!s){core.resources().release(reservation);return "memory_low";}s->reservation=reservation;s->owner=core.callerOwner();s->id=nextWs++;
  s->incoming=xQueueCreate(WsIncoming,sizeof(WsMessage));s->outgoing=xQueueCreate(2,sizeof(WsMessage));if(!s->incoming||!s->outgoing){delete s;return "memory_low";}
  s->ca=ca;s->headers=extra;s->client.onEvent([s](WStype_t t,uint8_t*p,size_t n){wsReceive(*s,t,p,n);});s->client.setReconnectInterval(retry);s->client.setExtraHeaders(s->headers.c_str());
  if(tls){if(a["verify_peer"]|true){if(s->ca.length())s->client.beginSslWithCA(host,port,path,s->ca.c_str(),"");else s->client.beginSslWithBundle(host,port,path,platformCaBundleStart,platformCaBundleEnd-platformCaBundleStart,"");}else s->client.beginSSL(host,port,path,nullptr,"");}else s->client.begin(host,port,path,"");
  s->residentBytes=sizeof(WsSession)+(WsIncoming+2)*sizeof(WsMessage)+2*sizeof(StaticQueue_t)+6144+s->ca.length()+s->headers.length();core.resources().used(reservation,s->residentBytes);
  if(xTaskCreate(wsTask,"notes-ws",6144,s,1,nullptr)!=pdPASS){delete s;return "task_allocation_failed";}
  wsSession=s;wsClient=&s->client;wsOwner=s->owner;out["connection_id"]=s->id;out["state"]="connecting";return nullptr;
 }
 if(!strcmp(m,"send")){
  if(!wsSession||!wsSession->connected||wsSession->stop)return "ws_not_connected";if(a.containsKey("text")==a.containsKey("hex"))return "one_ws_payload_required";
  WsMessage message;message.binary=a.containsKey("hex");const char*key=message.binary?"hex":"text";
  if(!a[key].is<const char*>()||strlen(a[key].as<const char*>())!=a[key].as<JsonString>().size())return "ws_payload_type";
  if(message.binary){if(!unhex(a[key],message.data,sizeof message.data,message.size))return "chunk_limit";}else{message.size=strlen(a[key]);if(message.size>sizeof message.data)return "chunk_limit";memcpy(message.data,a[key].as<const char*>(),message.size);}
  message.generation=wsSession->generation.load();message.id=nextWsMessage++;if(xQueueSend(wsSession->outgoing,&message,0)!=pdTRUE)return "ws_send_queue_full";
  out["message_id"]=message.id;out["connection_id"]=wsSession->id;out["queued_bytes"]=message.size;out["state"]="queued";return nullptr;
 }
 if(!strcmp(m,"receive")){if(!wsSession)return "ws_not_open";wsSession->readable=false;WsMessage message;if(xQueueReceive(wsSession->incoming,&message,0)!=pdTRUE){out["available"]=false;return nullptr;}char encoded[2049];hex(message.data,message.size,encoded);out["available"]=true;out["binary"]=message.binary;out["hex"]=encoded;out["connection_id"]=wsSession->id;return nullptr;}
 if(strcmp(m,"read"))return "unsupported_method";out["waiting_for_memory"]=wsSession&&wsSession->waitingMemory.load();out["started"]=wsSession!=nullptr;out["connected"]=wsSession&&wsSession->connected;out["closing"]=wsSession&&wsSession->stop;out["dropped_messages"]=wsSession?wsSession->drops.load():0;out["message_limit_bytes"]=1024;out["send_queue_capacity"]=2;
 if(wsSession){out["connection_id"]=wsSession->id;out["owner"]=wsOwner;out["buffered_messages"]=uxQueueMessagesWaiting(wsSession->incoming);out["queued_sends"]=uxQueueMessagesWaiting(wsSession->outgoing);}return nullptr;
}

#ifdef WISHOS_WS_DEFAULT_SOURCE
#undef WISHOS_WS_SOURCE
#undef WISHOS_WS_DEFAULT_SOURCE
#endif
