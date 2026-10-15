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
// Reachability probes run outside the event loop and never send an RPC or mutate
// a device. TLS/authentication still belongs to the selected full WS connection.
class LinkProbe {
 char host_[254]{};uint16_t port_=0;uint32_t id_=0;bool ready_=false;std::atomic<bool>reachable_{false};
 struct Entry{void(*run)(void*);void*context;};
 static void task(void*p){auto e=*static_cast<Entry*>(p);delete static_cast<Entry*>(p);e.run(e.context);vTaskDelete(nullptr);}
 static const char*work(void*p,const std::atomic<bool>&cancel){auto&s=*static_cast<LinkProbe*>(p);s.reachable_=false;if(cancel)return "cancelled";ProxyClient client(networkProxy.snapshot());bool ok=client.connect(s.host_,s.port_,1500);client.stop();if(cancel)return "cancelled";s.reachable_=ok;return ok?nullptr:"unreachable";}
 NativeOperation operation_;
public:
 LinkProbe():operation_(core,nullptr,{[](void(*run)(void*),void*p){auto e=new(std::nothrow)Entry{run,p};if(!e)return false;if(xTaskCreate(task,"wish-link-probe",6144,e,1,nullptr)!=pdPASS){delete e;return false;}return true;},8192},this,work){}
 bool active()const{return operation_.active();}
 bool ready()const{return ready_;}
 bool take(){ready_=false;return reachable_.load();}
 void poll(){bool running=active();operation_.poll();if(running&&!active())ready_=true;}
 void cancel(){JsonDocument a;a["operation_id"]=id_;operation_.cancel(a.as<JsonObjectConst>());ready_=false;}
 const char*start(JsonObjectConst endpoint){if(active())return "operation_active";ready_=false;copy(host_,sizeof host_,endpoint["host"]|"");port_=endpoint["port"]|(endpoint["tls"]==true?443u:80u);JsonDocument out;auto e=operation_.start("probe",out.to<JsonObject>());if(!e)id_=out["operation_id"];return e;}
};
static LinkProbe linkProbe;
static JsonDocument linkDesired,linkApplied;static uint32_t linkRevision=1,linkAppliedRevision=0;
static bool linkSuspended=false,linkPending=false,linkSaved=true,linkRestoreValid=true,linkClosing=false,linkEverConnected=false,linkAnnounced=false;
static unsigned linkIndex=0,linkNext=0,linkProbeIndex=0,linkProbeCursor=0;static uint32_t linkProbeRevision=0;
static uint64_t linkDeadline=0,linkRetryAt=0,linkProbeAt=0;static int linkPromote=-1;static char linkError[80]{};
static const char*validateLinkTransport(JsonObjectConst a){JsonDocument out;return serviceSocket::wsHandler("validate_connect",a,out.to<JsonObject>());}
static const char*validateLink(JsonObjectConst a){return LinkEndpoints::validate(a,validateLinkTransport);}
static bool serviceLinkActive(){return serviceSocket::webSocketActive()||linkProbe.active();}
static void closeLink(unsigned next,uint64_t retryAt){linkClosing=true;linkNext=next;linkRetryAt=retryAt;serviceSocket::stopWebSocket();serviceRpc.disconnect();linkAnnounced=false;}
static void pollServiceLink(bool suspend=false){
 auto now=core.now();linkProbe.poll();
 if(suspend&&!linkSuspended){serviceSocket::stopWebSocket();serviceRpc.disconnect();linkProbe.cancel();}if(!suspend&&linkSuspended)linkPending=true;linkSuspended=suspend;
 serviceSocket::pollWebSocket();if(suspend)return;
 if(linkPending){
  // A configuration call can arrive on this very RPC channel. Drain its reply
  // before stopping the native transport; never free RpcLink inside dispatch.
  if(serviceSocket::wsSession&&!serviceSocket::wsSession->stop){serviceRpc.poll(false);if(!serviceRpc.outboundDrained()||uxQueueMessagesWaiting(serviceSocket::wsSession->outgoing))return;serviceSocket::stopWebSocket();}
  if(serviceSocket::wsSession||linkProbe.active())return;serviceRpc.disconnect();linkApplied.set(linkDesired);linkAppliedRevision=linkRevision;linkPending=false;linkClosing=false;linkIndex=linkNext=linkProbeCursor=0;linkRetryAt=now;linkPromote=-1;linkProbeAt=0;linkAnnounced=false;linkError[0]=0;}
 auto config=linkApplied.as<JsonObjectConst>();unsigned count=LinkEndpoints::count(config);if(!count)return;
 uint32_t retry=config["retry_ms"]|5000u,probeInterval=config["probe_interval_ms"]|60000u;
 if(!serviceSocket::wsSession){if(linkClosing){linkIndex=linkNext;linkClosing=false;}if(now<linkRetryAt)return;JsonDocument args,out;LinkEndpoints::endpoint(config,linkIndex,args.to<JsonObject>());if(!args.containsKey("reconnect_ms"))args["reconnect_ms"]=retry;
  auto error=serviceSocket::wsHandler("connect",args.as<JsonObjectConst>(),out.to<JsonObject>());if(error){copy(linkError,sizeof linkError,error);linkRetryAt=now+retry;return;}linkEverConnected=false;linkDeadline=now+(config["attempt_timeout_ms"]|20000u);linkProbeAt=now+probeInterval;
 }
 if(linkClosing)return;
 auto socket=serviceSocket::wsSession;
 if(!socket->connected){serviceRpc.poll();if(socket->waitingMemory.load()){linkDeadline=now+(config["attempt_timeout_ms"]|20000u);return;}
  if(socket->failures.load()||now>=linkDeadline){copy(linkError,sizeof linkError,socket->failures.load()?"endpoint_disconnected":"endpoint_attempt_timeout");unsigned next=linkEverConnected&&linkIndex?0:(linkIndex+1)%count;closeLink(next,now+(next?0:retry));}return;
 }
 linkEverConnected=true;serviceRpc.poll();if(linkPending)return;
 if(serviceRpc.ready()&&!linkAnnounced){linkAnnounced=true;linkError[0]=0;JsonDocument d;d["endpoint_id"]=LinkEndpoints::id(config,linkIndex);d["index"]=linkIndex;d["revision"]=linkAppliedRevision;core.emit("/services/link","route_changed",d.as<JsonObjectConst>());}
 if(linkProbe.ready()){bool reachable=linkProbe.take();if(linkProbeRevision==linkAppliedRevision&&linkProbeIndex<linkIndex&&reachable)linkPromote=linkProbeIndex;}
 if(linkPromote>=0&&serviceRpc.quiescent(now,config["idle_ms"]|250u)&&!uxQueueMessagesWaiting(socket->incoming)&&!uxQueueMessagesWaiting(socket->outgoing)){unsigned next=linkPromote;linkPromote=-1;closeLink(next,now);return;}
 if(linkIndex&&probeInterval&&now>=linkProbeAt&&!linkProbe.active()&&linkPromote<0){linkProbeIndex=linkProbeCursor++%linkIndex;linkProbeRevision=linkAppliedRevision;JsonDocument candidate;LinkEndpoints::endpoint(config,linkProbeIndex,candidate.to<JsonObject>());linkProbe.start(candidate.as<JsonObjectConst>());linkProbeAt=now+probeInterval;}
}
static const char*linkHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"configure")){
  if(auto e=argumentFields(a,"settings,persist"))return e;if(!a["settings"].is<JsonObjectConst>())return "link_configuration_type";
  if(auto e=validateLink(a["settings"]))return e;JsonDocument next;next.set(a["settings"]);if(next.overflowed())return "memory_low";
  bool persist=a["persist"]|false;if(persist){String saved;serializeJson(next,saved);if(preferences.putString("service_link",saved)!=saved.length())return "persistence_failed";}
  swap(linkDesired,next);++linkRevision;linkSaved=persist;linkRestoreValid=true;linkPending=true;linkProbe.cancel();
 }else if(strcmp(m,"read"))return "unsupported_method";else if(a.size())return "unknown_link_argument";
 out["desired"]=linkDesired;JsonDocument endpoint;if(LinkEndpoints::count(linkApplied.as<JsonObjectConst>()))LinkEndpoints::endpoint(linkApplied.as<JsonObjectConst>(),linkIndex,endpoint.to<JsonObject>());
 auto applied=out["applied"].to<JsonObject>();applied["enabled"]=linkApplied["enabled"]|false;for(auto key:{"host","port","path","tls","verify_peer"})if(endpoint.containsKey(key))applied[key]=endpoint[key];
 out["endpoint_id"]=LinkEndpoints::id(linkApplied.as<JsonObjectConst>(),linkIndex);out["endpoint_index"]=linkIndex;out["endpoint_count"]=LinkEndpoints::count(linkApplied.as<JsonObjectConst>());out["revision"]=linkRevision;out["applied_revision"]=linkAppliedRevision;
 out["activation_pending"]=linkPending;out["suspended"]=linkSuspended;out["connected"]=serviceRpc.ready();out["closing"]=linkClosing;out["probe_active"]=linkProbe.active();out["retry_at_ms"]=linkRetryAt;out["waiting_for_memory"]=serviceSocket::wsSession&&serviceSocket::wsSession->waitingMemory.load();out["error"]=linkError;out["persisted"]=linkSaved;out["restore_valid"]=linkRestoreValid;return nullptr;
}
static void restoreServiceLink(){JsonDocument saved;auto text=preferences.getString("service_link","{\"enabled\":false}");if(deserializeJson(saved,text)||validateLink(saved.as<JsonObjectConst>())){linkRestoreValid=false;saved.clear();saved["enabled"]=false;}swap(linkDesired,saved);linkPending=true;}
static void linkDescription(JsonObject o){o["scope"]="system_connection";o["apply_time"]="after_previous_connection_and_probe_close";o["protocol"]="wishos-rpc/1";o["transport"]="WS/WSS";o["local_control_endpoint"]="/os/invoke";o["controls_network_association"]=false;o["message_limit_bytes"]=MAX_WIRE;o["settings_replace"]=true;o["endpoint_limit"]=4;o["configuration_limit_bytes"]=MAX_WIRE-1024;o["selection"]="ordered_failover_with_idle_preferred_reprobe";o["probe_scope"]="TCP reachability only; TLS and authentication apply to full connection";o["attempt_cancellation"]="cooperative_native_transport_boundary";
#if CONFIG_LWIP_DNS_SUPPORT_MDNS_QUERIES
 o["hostname_resolution"]="DNS_and_mDNS_local";
#else
 o["hostname_resolution"]="DNS";
#endif
 auto policy=o["policy"].to<JsonObject>();policy["attempt_timeout_ms"]=20000;policy["retry_ms"]=5000;policy["probe_interval_ms"]=60000;policy["idle_ms"]=250;auto events=o["events"].to<JsonArray>();for(auto event:{"connected","disconnected","failed","route_changed"})events.add(event);
}
