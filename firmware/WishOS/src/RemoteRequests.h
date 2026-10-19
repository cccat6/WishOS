#pragma once
#include "Core.h"
#include <new>
namespace notesos {
// Transport-independent outbox. A relay peeks, dispatches with the same ID and completes it.
class RemoteRequests {
public:
 static constexpr size_t PayloadLimit=MAX_WIRE-512,ResultLimit=1536;
private:
 struct Request{uint32_t id=0,owner=0;uint64_t expires=0;bool done=false,ok=false;char key[65]{},route[65]{};char*payload=nullptr,*result=nullptr,*progress=nullptr;uint32_t progressSequence=0;uint64_t progressAt=0;};
 Request*slots_[4]{};uint32_t next_=1;Core&core_;bool registered_=false;
 void release(Request*&r){if(r){core_.resources().freeAllocation(r->payload);core_.resources().freeAllocation(r->result);core_.resources().freeAllocation(r->progress);r->~Request();core_.resources().freeAllocation(r);r=nullptr;}}
 void changed(Request&r,const char*name){JsonDocument d;d["id"]=r.id;d["owner"]=r.owner;d["request_id"]=r.key;d["route"]=r.route;d["ok"]=r.ok;if(r.progressSequence)d["sequence"]=r.progressSequence;core_.emit("/services/remote",name,d.as<JsonObjectConst>(),r.owner);}
 static bool equivalent(const char*saved,JsonVariantConst value){JsonDocument d;return !deserializeJson(d,saved)&&d.as<JsonVariantConst>()==value;}
 static void describe(const Request&r,JsonObject o,bool includePayload=true,bool includeProgress=false){o["id"]=r.id;o["request_id"]=r.key;o["route"]=r.route;o["state"]=r.done?(r.ok?"completed":"failed"):"queued";o["ok"]=r.ok;
   if(includeProgress&&r.progress){JsonDocument p;deserializeJson(p,r.progress);o["progress"]=p;o["progress_sequence"]=r.progressSequence;o["progress_at_ms"]=r.progressAt;}
   JsonDocument d;if((r.done||includePayload)&&!deserializeJson(d,r.done?(r.result?r.result:"{\"error\":\"deadline_exceeded\"}"):r.payload))o[r.done?"result":"payload"]=d.as<JsonVariantConst>();}
public:
 static void describe(JsonObject o){remoteAsyncDescription(o);o["payload_limit_bytes"]=PayloadLimit;o["completion_limit_bytes"]=ResultLimit;o["allocation"]="actual_serialized_request_and_result";auto p=o["progress"].to<JsonObject>();p["format"]="wishos-progress/1";p["method"]="progress";p["text_limit_bytes"]=512;p["min_interval_ms"]=1000;p["serialized_limit_bytes"]=1024;p["read_options"]="include_payload:false for bounded status and progress";p["event_payload"]="id,request_id,sequence; read retains full progress";}
 explicit RemoteRequests(Core&c):core_(c){registered_=core_.onOwnerRelease(this,[](void*x,uint32_t owner){auto&s=*static_cast<RemoteRequests*>(x);for(auto&r:s.slots_)if(r&&r->owner==owner)s.release(r);});}
 ~RemoteRequests(){core_.removeOwnerHook(this);for(auto&r:slots_)release(r);}
 bool pending()const{for(auto r:slots_)if(r&&!r->done)return true;return false;}
 void tick(uint64_t now){for(auto r:slots_)if(r&&!r->done&&r->expires<=now){r->done=true;changed(*r,"failed");}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out,uint64_t now,const char*boot){
   const char*fields=!strcmp(m,"submit")?"route,request_id,payload,timeout_ms":!strcmp(m,"peek")?"after_id":!strcmp(m,"complete")?"id,ok,result":!strcmp(m,"progress")?"id,request_id,sequence,progress":!strcmp(m,"read")?"id,include_payload":!strcmp(m,"release")?"id":"";
   for(auto f:a)if(!Core::containsMethod(fields,f.key().c_str()))return "unknown_remote_argument";
   if(a.containsKey("id")&&!a["id"].is<uint32_t>())return "remote_request_type";if(a.containsKey("include_payload")&&!a["include_payload"].is<bool>())return "remote_request_type";
   if(strcmp(m,"read")&&strcmp(m,"peek"))tick(now);
   if(!strcmp(m,"submit")){
     if(!registered_)return "owner_hook_capacity";
     for(auto key:{"route","request_id"})if(a.containsKey(key)&&(!a[key].is<const char*>()||a[key].as<JsonString>().size()!=strlen(a[key].as<const char*>())))return "remote_request_type";
     if(a.containsKey("timeout_ms")&&!a["timeout_ms"].is<uint32_t>())return "remote_request_type";
     const char*key=a["request_id"]|"",*route=a["route"]|"";uint32_t ttl=a["timeout_ms"]|300000u;
     if(!*route||strlen(route)>64||strlen(key)>64||ttl<100||ttl>86400000)return "remote_request_range";
     if(!a["payload"].is<JsonObjectConst>()||measureJson(a["payload"])>PayloadLimit)return "remote_payload_limit";
     if(*key)for(auto r:slots_)if(r&&!strcmp(r->key,key)){
       if(core_.callerOwner()&&r->owner!=core_.callerOwner())return "request_owned_by_other";
       if(strcmp(r->route,route)||!equivalent(r->payload,a["payload"]))return "request_id_conflict";describe(*r,out);return nullptr;}
     Request**slot=nullptr;for(auto&s:slots_)if(!s){slot=&s;break;}if(!slot)return "remote_queue_full";
     if(!next_)return "request_id_exhausted";auto memory=core_.resources().allocate(sizeof(Request));if(!memory)return "remote_memory_budget";auto r=new(memory)Request();r->payload=static_cast<char*>(core_.resources().allocate(measureJson(a["payload"])+1));if(!r->payload){release(r);return "remote_memory_budget";}*slot=r;r->id=next_++;r->owner=core_.callerOwner();r->expires=now+ttl;strcpy(r->route,route);serializeJson(a["payload"],r->payload,measureJson(a["payload"])+1);
     if(*key)strcpy(r->key,key);else{size_t n=strlen(boot);if(n>48)n=48;memcpy(r->key,boot,n);r->key[n++]=':';char digits[10];unsigned count=0;uint32_t value=r->id;do{digits[count++]=char('0'+value%10);value/=10;}while(value);while(count)r->key[n++]=digits[--count];r->key[n]=0;}
     describe(*r,out);changed(*r,"queued");return nullptr;
   }
   if(!strcmp(m,"peek")){if(a.containsKey("after_id")&&!a["after_id"].is<uint32_t>())return "remote_cursor_type";uint32_t after=a["after_id"]|0u;Request*oldest=nullptr;for(auto r:slots_)if(r&&r->id>after&&!r->done&&(!core_.callerOwner()||r->owner==core_.callerOwner())&&(!oldest||r->id<oldest->id))oldest=r;if(oldest)describe(*oldest,out);else out["state"]="empty";return nullptr;}
   uint32_t id=a["id"]|0u;
   if(!strcmp(m,"read")&&!id){out["pending"]=pending();auto list=out["requests"].to<JsonArray>();for(auto r:slots_)if(r){auto o=list.add<JsonObject>();o["id"]=r->id;o["route"]=r->route;o["state"]=r->done?"finished":"queued";}out["capacity"]=4;out["persistent"]=false;return nullptr;}
   Request*r=nullptr;for(auto s:slots_)if(s&&s->id==id&&id)r=s;if(!r)return "request_not_found";
   if(core_.callerOwner()&&r->owner!=core_.callerOwner())return "request_owned_by_other";
   if(!strcmp(m,"progress")){
     if(r->done)return "request_finished";
     if(!a["request_id"].is<const char*>()||strcmp(r->key,a["request_id"]))return "request_id_conflict";
     auto p=a["progress"].as<JsonObjectConst>();
     if(!a["sequence"].is<uint32_t>()||!a["sequence"].as<uint32_t>()||p.isNull())return "remote_progress_type";
     if(auto e=argumentFields(p,"format,stage,source,text"))return e;
     if(p["format"]!="wishos-progress/1"||!p["stage"].is<const char*>()||!Core::containsMethod("received,receiving,transcribing,executing,preparing,transferring",p["stage"])||!p["source"].is<const char*>()||!Core::containsMethod("hub,harness",p["source"]))return "remote_progress_format";
     if(p.containsKey("text")&&(!p["text"].is<const char*>()||p["text"].as<JsonString>().size()>512||p["text"].as<JsonString>().size()!=strlen(p["text"].as<const char*>())))return "remote_progress_text";
     if(measureJson(p)>1024)return "remote_progress_limit";
     uint32_t seq=a["sequence"];if(seq<r->progressSequence)return "remote_progress_stale";
     if(seq==r->progressSequence){if(!r->progress||!equivalent(r->progress,p))return "remote_progress_conflict";}
     else{size_t n=measureJson(p)+1;auto saved=static_cast<char*>(core_.resources().allocate(n));if(!saved)return "remote_memory_budget";serializeJson(p,saved,n);core_.resources().freeAllocation(r->progress);r->progress=saved;r->progressSequence=seq;r->progressAt=now;changed(*r,"progress");}
   }else if(!strcmp(m,"complete")){
     if(!a["ok"].is<bool>()||!a.containsKey("result")||measureJson(a["result"])>ResultLimit)return "remote_result_limit";
     if(r->done){if(r->ok!=a["ok"].as<bool>()||!equivalent(r->result?r->result:"{\"error\":\"deadline_exceeded\"}",a["result"]))return "completion_conflict";}
     else{auto bytes=measureJson(a["result"])+1;auto result=static_cast<char*>(core_.resources().allocate(bytes));if(!result)return "remote_memory_budget";serializeJson(a["result"],result,bytes);r->result=result;r->done=true;r->ok=a["ok"];changed(*r,r->ok?"completed":"failed");}
   }else if(!strcmp(m,"release")){if(!r->done)return "request_pending";for(auto&s:slots_)if(s==r){release(s);break;}return nullptr;}
   else if(strcmp(m,"read"))return "unsupported_method";
   describe(*r,out,strcmp(m,"progress")&&(a["include_payload"]|true),!strcmp(m,"progress")||!strcmp(m,"read"));if(measureJson(out)>MAX_WIRE-256)return "remote_read_limit";return nullptr;
 }
};
}
