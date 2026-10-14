// A system-owned RPC transport survives application replacement. Passive control
// uses the already available HTTP/CDC envelopes. Active control uses the same ABI
// over a native WS/WSS client; no Harness or UI policy is embedded here.
namespace serviceSocket {
#define WISHOS_WS_SOURCE "/services/link"
#define WISHOS_WS_CONTROL
#include "WebSocketService.h"
#undef WISHOS_WS_SOURCE
#undef WISHOS_WS_CONTROL
}
static RpcLink serviceRpc(core,{nullptr,
 [](void*){return serviceSocket::wsSession&&serviceSocket::wsSession->connected.load()&&!serviceSocket::wsSession->stop.load();},
 [](void*){return serviceSocket::wsSession?serviceSocket::wsSession->generation.load():0u;},
 [](void*,const uint8_t*p,size_t n){auto s=serviceSocket::wsSession;if(!s||n>1024)return false;serviceSocket::WsMessage m;m.binary=true;m.size=n;m.generation=s->generation.load();memcpy(m.data,p,n);return xQueueSend(s->outgoing,&m,0)==pdTRUE;},
 [](void*,uint8_t*p,size_t&n){auto s=serviceSocket::wsSession;if(!s)return false;serviceSocket::WsMessage m;if(xQueueReceive(s->incoming,&m,0)!=pdTRUE)return false;if(!m.binary||m.generation!=s->generation.load()||m.size>n)return false;n=m.size;memcpy(p,m.data,n);return true;}
},bootId);
static JsonDocument linkDesired,linkApplied;static uint32_t linkRevision=1,linkAppliedRevision=0;static bool linkSuspended=false;static bool linkPending=false,linkSaved=true,linkRestoreValid=true;static char linkError[80]{};
static const char*validateLink(JsonObjectConst a){
 for(auto f:a)if(!Core::containsMethod("enabled,host,path,ca_pem,tls,verify_peer,port,reconnect_ms,headers",f.key().c_str()))return "unknown_link_setting";
 if(!a["enabled"].is<bool>())return "link_enabled_required";
 if(!a["enabled"].as<bool>())return a.size()==1?nullptr:"disabled_link_settings";
 JsonDocument transport,result;transport.set(a);transport.remove("enabled");return serviceSocket::wsHandler("validate_connect",transport.as<JsonObjectConst>(),result.to<JsonObject>());
}
static bool serviceLinkActive(){return serviceSocket::webSocketActive();}
static void pollServiceLink(bool suspend=false){
 if(suspend&&!linkSuspended){serviceSocket::stopWebSocket();serviceRpc.disconnect();}if(!suspend&&linkSuspended)linkPending=true;linkSuspended=suspend;
 serviceSocket::pollWebSocket();if(suspend)return;
 if(linkPending&&!serviceSocket::wsSession){linkApplied.set(linkDesired);linkAppliedRevision=linkRevision;linkPending=false;linkError[0]=0;
  if(linkDesired["enabled"]==true){JsonDocument a,r;a.set(linkDesired);a.remove("enabled");const char*e=serviceSocket::wsHandler("connect",a.as<JsonObjectConst>(),r.to<JsonObject>());if(e)copy(linkError,sizeof linkError,e);}
 }
 serviceRpc.poll();
}
static const char*linkHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"configure")){
  for(auto f:a)if(!Core::containsMethod("settings,persist",f.key().c_str()))return "unknown_link_argument";
  if(!a["settings"].is<JsonObjectConst>()||(a.containsKey("persist")&&!a["persist"].is<bool>()))return "link_configuration_type";
  if(auto e=validateLink(a["settings"]))return e;JsonDocument next;next.set(a["settings"]);if(next.overflowed())return "memory_low";
  bool persist=a["persist"]|false;if(persist){String saved;serializeJson(next,saved);if(preferences.putString("service_link",saved)!=saved.length())return "persistence_failed";}
  swap(linkDesired,next);++linkRevision;linkSaved=persist;linkRestoreValid=true;linkPending=true;serviceSocket::stopWebSocket();serviceRpc.disconnect();
 }else if(strcmp(m,"read"))return "unsupported_method";else if(a.size())return "unknown_link_argument";
 out["desired"]=linkDesired;auto applied=out["applied"].to<JsonObject>();for(auto key:{"enabled","host","port","path","tls","verify_peer"})if(linkApplied.containsKey(key))applied[key]=linkApplied[key];out["revision"]=linkRevision;out["applied_revision"]=linkAppliedRevision;out["activation_pending"]=linkPending;out["suspended"]=linkSuspended;out["connected"]=serviceRpc.ready();out["error"]=linkError;out["persisted"]=linkSaved;out["restore_valid"]=linkRestoreValid;return nullptr;
}
static void restoreServiceLink(){JsonDocument saved;auto text=preferences.getString("service_link","{\"enabled\":false}");if(deserializeJson(saved,text)||validateLink(saved.as<JsonObjectConst>())){linkRestoreValid=false;saved.clear();saved["enabled"]=false;}swap(linkDesired,saved);linkPending=true;}
static void linkDescription(JsonObject o){o["scope"]="system_connection";o["apply_time"]="after_previous_connection_closes";o["protocol"]="wishos-rpc/1";o["transport"]="WS/WSS";o["local_control_endpoint"]="/os/invoke";o["controls_network_association"]=false;o["message_limit_bytes"]=MAX_WIRE;o["settings_replace"]=true;o["events"].to<JsonArray>().add("connected");o["events"].add("disconnected");o["events"].add("failed");}
