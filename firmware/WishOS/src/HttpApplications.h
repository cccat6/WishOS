#pragma once
#include "Core.h"
#include "Policy.h"
#include <new>
namespace notesos {
class HttpApplications {
 struct Route{char path[96]{},method[8]{},type[64]{},body[2049]{};uint16_t status=200;bool dynamic=false;uint32_t timeout=10000,owner=0;};
 struct Request{uint32_t id=0,owner=0;uint64_t deadline=0;char path[96]{},method[8]{},body[2049]{},type[64]="application/json";uint16_t status=202;bool done=false;};
 Route*routes_[6]{};Request*requests_[4]{};uint32_t next_=1;Core&core_;bool registered_=false;
 Request*find(uint32_t id){for(auto r:requests_)if(r&&r->id==id)return r;return nullptr;}
public:
 explicit HttpApplications(Core&c):core_(c){registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){((HttpApplications*)p)->releaseOwner(owner);});}
 ~HttpApplications(){core_.removeOwnerHook(this);for(auto r:routes_)delete r;for(auto r:requests_)delete r;}
 void releaseOwner(uint32_t owner){for(auto&r:routes_)if(r&&r->owner==owner){delete r;r=nullptr;}for(auto&r:requests_)if(r&&r->owner==owner){delete r;r=nullptr;}}
 bool active()const{for(auto r:routes_)if(r)return true;return false;}
 void clear(){for(auto&r:routes_){delete r;r=nullptr;}for(auto&r:requests_){delete r;r=nullptr;}}
 void tick(uint64_t now){for(auto r:requests_)if(r&&!r->done&&now>=r->deadline){r->done=true;r->status=504;strcpy(r->body,"{\"error\":\"application_timeout\"}");}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out){
   const char*allowed=!strcmp(m,"route")?"path,method,body,content_type,status,timeout_ms,dynamic":!strcmp(m,"respond")?"id,body,content_type,status":!strcmp(m,"remove")?"path":!strcmp(m,"stop")?"":"id";
   for(auto field:a)if(!Policies::choice(allowed,field.key().c_str()))return "unknown_http_route_argument";
   for(auto key:{"path","method","body","content_type"})if(a.containsKey(key)&&(!a[key].is<const char*>()||strlen(a[key].as<const char*>())!=a[key].as<JsonString>().size()))return "http_route_string_required";
   for(auto key:{"id","status","timeout_ms"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "http_route_integer_required";
   if(a.containsKey("dynamic")&&!a["dynamic"].is<bool>())return "http_route_boolean_required";
   if(!strcmp(m,"stop")){if(core_.callerOwner())releaseOwner(core_.callerOwner());else clear();return nullptr;}
   if(!strcmp(m,"route")){
     if(!registered_)return "owner_hook_capacity";
     const char*path=a["path"]|"",*method=a["method"]|"GET",*body=a["body"]|"",*type=a["content_type"]|"application/json";
     uint32_t status=a["status"]|200u,timeout=a["timeout_ms"]|10000u;
     if((strcmp(path,"/")&&strncmp(path,"/app/",5))||strlen(path)>=96||strchr(path,'?')||strlen(body)>2048||strlen(type)>=64||strpbrk(type,"\r\n")||!Policies::choice("GET,POST,PUT,DELETE",method)||status<200||status>599||timeout<100||timeout>60000)return "http_route_range";
     Route**slot=nullptr;for(auto&r:routes_)if(r&&!strcmp(r->path,path)&&!strcmp(r->method,method)){slot=&r;break;}if(!slot)for(auto&r:routes_)if(!r){slot=&r;break;}if(!slot)return "route_capacity";
     if(*slot&&core_.callerOwner()&&(*slot)->owner!=core_.callerOwner())return "route_owned_by_other";
     auto r=new(std::nothrow) Route;if(!r)return "memory_low";r->owner=core_.callerOwner();strcpy(r->path,path);strcpy(r->method,method);strcpy(r->body,body);strcpy(r->type,type);r->status=status;r->dynamic=a["dynamic"]|false;r->timeout=timeout;delete *slot;*slot=r;return nullptr;
   }
   if(!strcmp(m,"remove")){for(auto r:routes_)if(r&&!strcmp(r->path,a["path"]|"")&&core_.callerOwner()&&r->owner!=core_.callerOwner())return "route_owned_by_other";for(auto&r:routes_)if(r&&!strcmp(r->path,a["path"]|"")){delete r;r=nullptr;}return nullptr;}
   if(!strcmp(m,"read")&&!a["id"].is<uint32_t>()){out["active"]=active();auto list=out["routes"].to<JsonArray>();for(auto r:routes_)if(r&&(!core_.callerOwner()||r->owner==core_.callerOwner())){auto o=list.add<JsonObject>();o["path"]=r->path;o["method"]=r->method;o["dynamic"]=r->dynamic;}auto q=out["requests"].to<JsonArray>();for(auto r:requests_)if(r&&(!core_.callerOwner()||r->owner==core_.callerOwner()))q.add(r->id);return nullptr;}
   auto r=find(a["id"]|0u);if(!r)return "request_not_found";if(core_.callerOwner()&&r->owner!=core_.callerOwner())return "request_owned_by_other";
   if(!strcmp(m,"respond")){const char*body=a["body"]|"",*type=a["content_type"]|"application/json";uint32_t status=a["status"]|200u;if(strlen(body)>2048||strlen(type)>=64||strpbrk(type,"\r\n")||status<200||status>599)return "http_response_range";if(r->done)return "request_finished";strcpy(r->body,body);strcpy(r->type,type);r->status=status;r->done=true;}
   else if(!strcmp(m,"release")){for(auto&slot:requests_)if(slot==r){delete slot;slot=nullptr;}return nullptr;}
   else if(strcmp(m,"read"))return "unsupported_method";
   out["id"]=r->id;out["path"]=r->path;out["method"]=r->method;out["body"]=r->body;out["done"]=r->done;out["status"]=r->status;out["content_type"]=r->type;return nullptr;
 }
 bool handle(const char*path,const char*method,const char*body,uint64_t now,JsonObject response){
   for(auto r:routes_)if(r&&!strcmp(r->path,path)&&!strcmp(r->method,method)){
     if(strlen(body)>2048){response["status"]=413;response["body"]="request too large";return true;}
     response["content_type"]=r->type;response["status"]=r->status;response["body"]=r->body;if(!r->dynamic)return true;
     Request**slot=nullptr;for(auto&q:requests_)if(!q){slot=&q;break;}if(!slot){response["status"]=503;response["body"]="application queue full";return true;}
     auto q=new(std::nothrow) Request;if(!q){response["status"]=503;response["body"]="memory low";return true;}q->id=next_++;q->owner=r->owner;q->deadline=now+r->timeout;strcpy(q->path,path);strcpy(q->method,method);strcpy(q->body,body);*slot=q;
     JsonDocument event;event["id"]=q->id;event["owner"]=q->owner;event["path"]=path;event["method"]=method;core_.emit("/net/http-server","request",event.as<JsonObjectConst>(),q->owner);
     char reply[96];snprintf(reply,sizeof reply,"{\"id\":%lu,\"state\":\"accepted\"}",(unsigned long)q->id);response["content_type"]="application/json";response["status"]=202;response["body"]=reply;return true;
   }return false;
 }
};
}
