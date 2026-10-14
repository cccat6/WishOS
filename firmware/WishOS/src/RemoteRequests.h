#pragma once
#include "Core.h"
namespace notesos {
// Transport-independent outbox. A relay peeks, dispatches with the same ID and completes it.
class RemoteRequests {
 struct Request{uint32_t id=0,owner=0;uint64_t expires=0;bool done=false,ok=false;char key[65]{},route[65]{},payload[1537]{},result[1537]{};};
 Request slots_[4];uint32_t next_=1;Core&core_;bool registered_=false;
 void changed(Request&r,const char*name){JsonDocument d;d["id"]=r.id;d["owner"]=r.owner;d["request_id"]=r.key;d["route"]=r.route;d["ok"]=r.ok;core_.emit("/services/remote",name,d.as<JsonObjectConst>(),r.owner);}
 static bool equivalent(const char*saved,JsonVariantConst value){JsonDocument d;return !deserializeJson(d,saved)&&d.as<JsonVariantConst>()==value;}
 static void describe(const Request&r,JsonObject o){o["id"]=r.id;o["request_id"]=r.key;o["route"]=r.route;o["state"]=r.done?(r.ok?"completed":"failed"):"queued";o["ok"]=r.ok;
   JsonDocument d;if(!deserializeJson(d,r.done?r.result:r.payload))o[r.done?"result":"payload"]=d.as<JsonVariantConst>();}
public:
 explicit RemoteRequests(Core&c):core_(c){registered_=core_.onOwnerRelease(this,[](void*x,uint32_t owner){for(auto&r:static_cast<RemoteRequests*>(x)->slots_)if(r.owner==owner)r={};});}
 ~RemoteRequests(){core_.removeOwnerHook(this);}
 bool pending()const{for(auto&r:slots_)if(r.id&&!r.done)return true;return false;}
 void tick(uint64_t now){for(auto&r:slots_)if(r.id&&!r.done&&r.expires<=now){r.done=true;strcpy(r.result,"{\"error\":\"deadline_exceeded\"}");changed(r,"failed");}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out,uint64_t now,const char*boot){
   const char*fields=!strcmp(m,"submit")?"route,request_id,payload,timeout_ms":!strcmp(m,"peek")?"after_id":!strcmp(m,"complete")?"id,ok,result":!strcmp(m,"read")||!strcmp(m,"release")?"id":"";
   for(auto f:a)if(!Core::containsMethod(fields,f.key().c_str()))return "unknown_remote_argument";
   if(a.containsKey("id")&&!a["id"].is<uint32_t>())return "remote_request_type";
   if(strcmp(m,"read")&&strcmp(m,"peek"))tick(now);
   if(!strcmp(m,"submit")){
     if(!registered_)return "owner_hook_capacity";
     for(auto key:{"route","request_id"})if(a.containsKey(key)&&(!a[key].is<const char*>()||a[key].as<JsonString>().size()!=strlen(a[key].as<const char*>())))return "remote_request_type";
     if(a.containsKey("timeout_ms")&&!a["timeout_ms"].is<uint32_t>())return "remote_request_type";
     const char*key=a["request_id"]|"",*route=a["route"]|"";uint32_t ttl=a["timeout_ms"]|300000u;
     if(!*route||strlen(route)>64||strlen(key)>64||ttl<100||ttl>86400000)return "remote_request_range";
     if(!a["payload"].is<JsonObjectConst>()||measureJson(a["payload"])>1536)return "remote_payload_limit";
     char payload[1537];serializeJson(a["payload"],payload,sizeof payload);
     if(*key)for(auto&r:slots_)if(r.id&&!strcmp(r.key,key)){
       if(core_.callerOwner()&&r.owner!=core_.callerOwner())return "request_owned_by_other";
       if(strcmp(r.route,route)||!equivalent(r.payload,a["payload"]))return "request_id_conflict";describe(r,out);return nullptr;}
     Request*r=nullptr;for(auto&s:slots_)if(!s.id){r=&s;break;}if(!r)return "remote_queue_full";
     if(!next_)return "request_id_exhausted";*r={};r->id=next_++;r->owner=core_.callerOwner();r->expires=now+ttl;strcpy(r->route,route);strcpy(r->payload,payload);
     if(*key)strcpy(r->key,key);else snprintf(r->key,sizeof r->key,"%s:%lu",boot,(unsigned long)r->id);
     describe(*r,out);changed(*r,"queued");return nullptr;
   }
   if(!strcmp(m,"peek")){if(a.containsKey("after_id")&&!a["after_id"].is<uint32_t>())return "remote_cursor_type";uint32_t after=a["after_id"]|0u;Request*oldest=nullptr;for(auto&r:slots_)if(r.id>after&&!r.done&&(!core_.callerOwner()||r.owner==core_.callerOwner())&&(!oldest||r.id<oldest->id))oldest=&r;if(oldest)describe(*oldest,out);else out["state"]="empty";return nullptr;}
   uint32_t id=a["id"]|0u;
   if(!strcmp(m,"read")&&!id){out["pending"]=pending();auto list=out["requests"].to<JsonArray>();for(auto&r:slots_)if(r.id){auto o=list.add<JsonObject>();o["id"]=r.id;o["route"]=r.route;o["state"]=r.done?"finished":"queued";}out["capacity"]=4;out["persistent"]=false;return nullptr;}
   Request*r=nullptr;for(auto&s:slots_)if(s.id==id&&id)r=&s;if(!r)return "request_not_found";
   if(core_.callerOwner()&&r->owner!=core_.callerOwner())return "request_owned_by_other";
   if(!strcmp(m,"complete")){
     if(!a["ok"].is<bool>()||!a.containsKey("result")||measureJson(a["result"])>1536)return "remote_result_limit";
     char result[1537];serializeJson(a["result"],result,sizeof result);
     if(r->done){if(r->ok!=a["ok"].as<bool>()||!equivalent(r->result,a["result"]))return "completion_conflict";}
     else{r->done=true;r->ok=a["ok"];strcpy(r->result,result);changed(*r,r->ok?"completed":"failed");}
   }else if(!strcmp(m,"release")){if(!r->done)return "request_pending";*r={};return nullptr;}
   else if(strcmp(m,"read"))return "unsupported_method";
   describe(*r,out);return nullptr;
 }
};
}
