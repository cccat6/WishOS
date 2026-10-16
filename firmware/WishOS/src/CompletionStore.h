#pragma once
#include "ResourceBudget.h"
#include <new>
namespace notesos {
// Result observations have their own lifetime. The lossy event journal only
// announces them. Native producer tasks may record here without touching Core.
template<size_t Sources>class CompletionStore {
 struct Pending {Pending*next=nullptr;uint64_t order=0,at=0;char event[40]{},data[512]{};};
 struct Record {
  Record*next=nullptr;uint32_t id=0,owner=0;size_t sourceIndex=0;uint64_t order=0,at=0;
  bool bound=false,ready=false,captureFailed=false;Pending*pending=nullptr;char source[48]{},events[128]{},where[256]{};
  char event[40]{},data[512]{};
 };
 ResourceBudget&memory_;Record*head_=nullptr;uint32_t next_=1;uint64_t order_=0;
 mutable std::recursive_mutex mutex_;std::atomic<unsigned>interest_[Sources]{};
 static bool choice(const char*csv,const char*value){for(auto p=csv;*p;){auto e=strchr(p,',');size_t n=e?size_t(e-p):strlen(p);if(strlen(value)==n&&!strncmp(p,value,n))return true;if(!e)break;p=e+1;}return false;}
 static bool matches(const Record&r,JsonObjectConst data){JsonDocument where;if(deserializeJson(where,r.where))return false;for(auto p:where.as<JsonObjectConst>())if(data[p.key()]!=p.value())return false;return true;}
 Record*find(uint32_t id)const{for(auto p=head_;p;p=p->next)if(p->id==id)return p;return nullptr;}
 void clearPending(Record&r){while(r.pending){auto p=r.pending;r.pending=p->next;p->~Pending();memory_.freeAllocation(p);}}
 void destroy(Record*p){clearPending(*p);--interest_[p->sourceIndex];p->~Record();memory_.freeAllocation(p);}
public:
 explicit CompletionStore(ResourceBudget&m):memory_(m){}
 ~CompletionStore(){while(head_){auto next=head_->next;destroy(head_);head_=next;}}
 std::recursive_mutex&mutex(){return mutex_;}
 const char*reserve(uint32_t owner,JsonObjectConst spec,uint32_t&id,size_t sourceIndex=Sources-1){
  std::lock_guard<std::recursive_mutex>lock(mutex_);const char*source=spec["source"]|"";auto events=spec["events"].as<JsonArrayConst>();
  if(!*source||strlen(source)>=48||!events.size()||!spec["match"].is<JsonObjectConst>())return "invalid_async_contract";
  char names[128]{};size_t at=0;
  for(auto e:events){const char*s=e|"";size_t n=strlen(s);if(!n||n>=40||strchr(s,',')||at+n+2>sizeof names)return "invalid_async_contract";if(at)names[at++]=',';memcpy(names+at,s,n+1);at+=n;}
  if(!next_)return "completion_id_exhausted";
  auto block=memory_.allocate(sizeof(Record));if(!block)return "completion_memory_budget";
  auto r=new(block)Record;r->id=next_++;r->owner=owner;r->sourceIndex=sourceIndex;++interest_[sourceIndex];strcpy(r->source,source);strcpy(r->events,names);r->next=head_;head_=r;id=r->id;return nullptr;
 }
 const char*bind(uint32_t id,JsonObjectConst spec,JsonObjectConst reply,uint64_t now){
  std::lock_guard<std::recursive_mutex>lock(mutex_);auto r=find(id);if(!r)return "completion_not_found";JsonDocument where;
  for(auto p:spec["match"].as<JsonObjectConst>()){const char*key=p.value()|"";auto value=reply[key];if(value.isNull()||value.is<JsonObjectConst>()||value.is<JsonArrayConst>())return "async_reply_missing_correlation";where[p.key()]=value;}
  if(where.overflowed()||measureJson(where)>=sizeof r->where)return "completion_correlation_limit";
  serializeJson(where,r->where,sizeof r->where);r->bound=true;
  if(r->ready){JsonDocument event;deserializeJson(event,r->data);if(!matches(*r,event.as<JsonObjectConst>()))r->ready=false;}
  for(auto p=r->pending;p;p=p->next){JsonDocument data;deserializeJson(data,p->data);if(matches(*r,data.as<JsonObjectConst>())&&(!r->ready||p->order<r->order)){strcpy(r->event,p->event);strcpy(r->data,p->data);r->order=p->order;r->at=p->at;r->ready=true;}}
  clearPending(*r);
  if(r->captureFailed&&!r->ready){strcpy(r->event,"delivery_error");JsonDocument data;data.set(where);data["error"]="completion_capture_memory_budget";serializeJson(data,r->data,sizeof r->data);r->ready=true;r->order=++order_;r->at=now;}
  auto settled=spec["settled"].as<JsonObjectConst>();const char*state=reply[settled["field"]|""]|"";const char*name=settled["events"][state]|"";
  if(*name&&!r->ready){JsonDocument event;event.set(where);event[settled["field"]|"state"]=state;for(auto key:{"ok","error"})if(reply.containsKey(key))event[key]=reply[key];if(event.overflowed()||measureJson(event)>=sizeof r->data)return "completion_result_limit";strncpy(r->event,name,sizeof r->event-1);serializeJson(event,r->data,sizeof r->data);r->ready=true;r->order=++order_;r->at=now;}
  return nullptr;
 }
 void record(const char*source,const char*event,JsonObjectConst data,uint32_t owner,uint64_t at,size_t sourceIndex=Sources-1){
  if(!interest_[sourceIndex].load())return;
  std::lock_guard<std::recursive_mutex>lock(mutex_);
  // System observations (owner 0) follow authorized calls into any view.
  // Application observations remain isolated by owner and correlation fields.
  for(auto r=head_;r;r=r->next)if((!r->owner||!owner||owner==r->owner)&&!strcmp(source,r->source)&&choice(r->events,event)&&(!r->bound||matches(*r,data))){
   if((r->bound&&r->ready)||measureJson(data)>=sizeof r->data)continue;
   if(!r->bound&&r->ready){auto block=memory_.allocate(sizeof(Pending));if(!block){r->captureFailed=true;continue;}auto p=new(block)Pending;p->next=r->pending;r->pending=p;strcpy(p->event,event);serializeJson(data,p->data,sizeof p->data);p->order=++order_;p->at=at;}
   else{strcpy(r->event,event);serializeJson(data,r->data,sizeof r->data);r->ready=true;r->order=++order_;r->at=at;}
  }
 }
 bool ready(uint32_t owner)const{std::lock_guard<std::recursive_mutex>lock(mutex_);for(auto r=head_;r;r=r->next)if(r->owner==owner&&r->bound&&r->ready)return true;return false;}
 const char*read(uint32_t id,uint32_t owner,JsonObject out)const{
  std::lock_guard<std::recursive_mutex>lock(mutex_);auto r=find(id);if(!r)return "completion_not_found";if(owner&&owner!=r->owner)return "completion_owned_by_other";
  out["id"]=r->id;out["owner"]=r->owner;out["ready"]=r->bound&&r->ready;
  if(r->bound&&r->ready){out["source"]=r->source;out["event"]=r->event;out["at_ms"]=r->at;out["completion_order"]=r->order;out["replayed"]=true;JsonDocument data;if(deserializeJson(data,r->data))return "completion_decode_failed";out["data"]=data.as<JsonVariantConst>();}return nullptr;
 }
 const char*release(uint32_t id,uint32_t owner){std::lock_guard<std::recursive_mutex>lock(mutex_);for(auto p=&head_;*p;p=&(*p)->next)if((*p)->id==id){auto r=*p;if(owner&&owner!=r->owner)return "completion_owned_by_other";*p=r->next;destroy(r);return nullptr;}return "completion_not_found";}
 void releaseOwner(uint32_t owner){std::lock_guard<std::recursive_mutex>lock(mutex_);for(auto p=&head_;*p;)if((*p)->owner==owner){auto r=*p;*p=r->next;destroy(r);}else p=&(*p)->next;}
};
}
