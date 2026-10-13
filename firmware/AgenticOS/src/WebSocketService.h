static WebSocketsClient*wsClient=nullptr;static bool wsConnected=false;static String wsCA;
struct WsMessage{bool binary=false;size_t size=0;uint8_t data[1024]{};};
static WsMessage*wsMessages[4]{};static uint32_t wsDrops=0,wsOwner=0;
static WsMessage*wsAssembly=nullptr;
static void wsEnqueue(WsMessage*value){for(auto&m:wsMessages)if(!m){m=value;event("/net/ws","readable");return;}delete value;++wsDrops;event("/net/ws","overflow");}
static void wsReceive(WStype_t type,uint8_t*data,size_t n){
 if(type==WStype_CONNECTED||type==WStype_DISCONNECTED){wsConnected=type==WStype_CONNECTED;if(!wsConnected){delete wsAssembly;wsAssembly=nullptr;}event("/net/ws",wsConnected?"connected":"disconnected");return;}
 bool initial=type==WStype_FRAGMENT_TEXT_START||type==WStype_FRAGMENT_BIN_START;
 bool more=type==WStype_FRAGMENT||type==WStype_FRAGMENT_FIN;
 bool whole=type==WStype_TEXT||type==WStype_BIN;if(!initial&&!more&&!whole)return;
 if(initial||whole){if(wsAssembly){delete wsAssembly;wsAssembly=nullptr;++wsDrops;}wsAssembly=new(std::nothrow) WsMessage;if(wsAssembly)wsAssembly->binary=type==WStype_BIN||type==WStype_FRAGMENT_BIN_START;}
 if(!wsAssembly||n>sizeof(wsAssembly->data)-wsAssembly->size){delete wsAssembly;wsAssembly=nullptr;++wsDrops;event("/net/ws","overflow");return;}
 memcpy(wsAssembly->data+wsAssembly->size,data,n);wsAssembly->size+=n;
 if(whole||type==WStype_FRAGMENT_FIN){auto complete=wsAssembly;wsAssembly=nullptr;wsEnqueue(complete);}
}
static void stopWebSocket(){if(wsClient){wsClient->disconnect();delete wsClient;wsClient=nullptr;}wsConnected=false;wsOwner=0;delete wsAssembly;wsAssembly=nullptr;wsCA="";for(auto&m:wsMessages){delete m;m=nullptr;}}
static const char*wsHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(wsClient&&strcmp(m,"read")&&core.callerOwner()&&core.callerOwner()!=wsOwner)return "ws_owned_by_other";
 if(!strcmp(m,"close")){stopWebSocket();return nullptr;}
 if(!strcmp(m,"connect")){
   if(wsClient)return "ws_already_open";const char*host=a["host"]|"",*path=a["path"]|"/",*ca=a["ca_pem"]|"";bool tls=a["tls"]|false;uint32_t port=a["port"]|(tls?443u:80u),retry=a["reconnect_ms"]|5000u;
   if(!*host||strlen(host)>253||strpbrk(host,"\r\n")||strpbrk(path,"\r\n")||path[0]!='/'||strlen(path)>512||port<1||port>65535||strlen(ca)>8192||retry<100)return "ws_configuration";if(tls&&!*ca)return "https_ca_required";
   String extra;for(auto field:a["headers"].as<JsonObjectConst>()){const char*value=field.value().as<const char*>();if(!value||strpbrk(field.key().c_str(),"\r\n:")||strpbrk(value,"\r\n"))return "invalid_header";extra+=field.key().c_str();extra+=": ";extra+=value;extra+="\r\n";if(extra.length()>512)return "header_limit";}
   wsClient=new(std::nothrow) WebSocketsClient;if(!wsClient)return "memory_low";wsOwner=core.callerOwner();wsCA=ca;wsClient->onEvent(wsReceive);wsClient->setReconnectInterval(retry);wsClient->setExtraHeaders(extra.c_str());
   if(tls)wsClient->beginSslWithCA(host,port,path,wsCA.c_str(),"");else wsClient->begin(host,port,path,"");out["state"]="connecting";return nullptr;
 }
 if(!strcmp(m,"send")){if(!wsConnected)return "ws_not_connected";uint8_t b[1024];size_t n=0;bool binary=a["hex"].is<const char*>();
   if(binary){if(!unhex(a["hex"],b,sizeof b,n))return "chunk_limit";}else{const char*t=a["text"]|"";n=strlen(t);if(n>sizeof b)return "chunk_limit";memcpy(b,t,n);}
   if(!(binary?wsClient->sendBIN(b,n):wsClient->sendTXT(b,n)))return "ws_send_failed";out["bytes"]=n;return nullptr;
 }
 if(!strcmp(m,"receive")){auto msg=wsMessages[0];if(!msg){out["available"]=false;return nullptr;}char h[2049];hex(msg->data,msg->size,h);out["available"]=true;out["binary"]=msg->binary;out["hex"]=h;delete msg;for(int i=0;i<3;++i)wsMessages[i]=wsMessages[i+1];wsMessages[3]=nullptr;return nullptr;}
 if(strcmp(m,"read"))return "unsupported_method";out["started"]=wsClient!=nullptr;out["connected"]=wsConnected;out["dropped_messages"]=wsDrops;out["message_limit_bytes"]=1024;return nullptr;
}
