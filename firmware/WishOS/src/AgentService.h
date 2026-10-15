#pragma once
#include "Applications.h"
#include "AssetTransfers.h"
namespace notesos {
// A result is an ordered program of application messages and system resource
// calls. The OS owns its lifetime; application messages have explicit recipients.
class AgentService {
 struct Task {
  uint32_t id=0,origin=0,remote=0,index=0,count=0,effect=0,recipient=0,follows=0,transfer=0;
  uint64_t deadline=0,position=0,fileSize=0;
  char state[24]{},error[80]{},key[65]{},operation[65]{},root[256]{},plan[256]{},effectPath[80]{},effectMethod[40]{};
  bool detached=false,filePlan=false,stop=false,inCall=false,appPending=false,ownsOperation=false;
  JsonDocument envelope,action,previous;
  Vfs::Access*lease=nullptr;FileProvider*provider=nullptr;uint8_t*line=nullptr;size_t lineSize=0,lineLimit=2048;
 };
 struct Receipt{char key[65]{},hash[65]{},error[80]{};bool used=false;uint32_t completed=0;};
 Core&core_;AssetTransfers&assets_;Vfs&vfs_;Task tasks_[4];Receipt receipts_[8];unsigned receiptAt_=0;uint32_t next_=1,owner_=0;JsonDocument config_;
 static bool terminal(const Task&t){return !strcmp(t.state,"completed")||!strcmp(t.state,"failed");}
 Task*find(uint32_t id){for(auto&t:tasks_)if(t.id&&t.id==id)return &t;return nullptr;}
 Receipt*receipt(const char*key){if(*key)for(auto&r:receipts_)if(r.used&&!strcmp(r.key,key))return &r;return nullptr;}
 static void digest(JsonVariantConst value,char out[65]){Sha256 hash;struct Writer{Sha256&h;size_t write(uint8_t b){h.update(&b,1);return 1;}size_t write(const uint8_t*p,size_t n){h.update(p,n);return n;}}writer{hash};serializeJson(value,writer);hash.finish(out);}
 void notify(Task&t,const char*event,uint32_t recipient=0){JsonDocument d;d["id"]=t.id;d["step"]=t.index;
  if(!strcmp(event,"application_action")){d["topic"]=t.action["topic"]|"result";if(t.action["wait"]=="queued")d["data"]=t.action["data"];}
  else{d["request_id"]=t.key;d["state"]=t.state;d["steps"]=t.count;d["ok"]=!strcmp(event,"completed");if(*t.error)d["error"]=t.error;}
  core_.emit("/services/ai",event,d.as<JsonObjectConst>(),recipient?recipient:t.origin&&!t.detached?t.origin:owner_);
 }
 void cleanup(Task&t){if(t.transfer){JsonDocument a,o;a["id"]=t.transfer;assets_.invoke("release",a.as<JsonObjectConst>(),o.to<JsonObject>());t.transfer=0;}delete t.lease;t.lease=nullptr;if(t.provider)vfs_.unpin(t.id+0x71000000u);t.provider=nullptr;core_.resources().freeAllocation(t.line);t.line=nullptr;assets_.cancelRemote(t.remote);}
 void finish(Task&t,const char*error=nullptr){copy(t.error,sizeof t.error,error?error:"");strcpy(t.state,error?"failed":"completed");t.appPending=false;t.recipient=0;
  if(t.ownsOperation&&*t.operation){auto*r=receipt(t.operation);if(!r)r=&receipts_[receiptAt_++%8];r->used=true;copy(r->key,sizeof r->key,t.operation);copy(r->error,sizeof r->error,t.error);digest(t.envelope.as<JsonVariantConst>(),r->hash);r->completed=t.index;}
  cleanup(t);notify(t,error?"failed":"completed");
 }
 const char*validateAction(JsonObjectConst a){const char*type=a["type"]|"";
  if(!strcmp(type,"system")){if(auto e=argumentFields(a,"type,path,method,args"))return e;const char*path=a["path"]|"",*method=a["method"]|"";auto d=core_.find(path);if(!d||!core_.supports(*d,method)||!a["args"].is<JsonObjectConst>())return "ai_system_action";}
  else if(!strcmp(type,"application")){if(auto e=argumentFields(a,"type,recipient,topic,data,wait"))return e;for(auto key:{"recipient","topic","wait"})if(a.containsKey(key)&&!a[key].is<const char*>())return "ai_application_action";if(!Core::containsMethod("origin,foreground",a["recipient"]|"origin")||!Core::containsMethod("processed,queued",a["wait"]|"processed")||!a["data"].is<JsonObjectConst>()||strlen(a["topic"]|"result")>64)return "ai_application_action";if(a["wait"]=="queued"&&measureJson(a["data"])>MAX_EVENT-256)return "ai_queued_message_limit";}
  else return "ai_action_type";
  return nullptr;
 }
 const char*envelope(Task&t,JsonObjectConst r){if(auto e=argumentFields(r,"format,operation_id,transfer_id,actions,sequence"))return e;
  if(r["format"]!="wishos-result/2"||r.containsKey("operation_id")&&(!r["operation_id"].is<const char*>()||strlen(r["operation_id"])>64))return "ai_result_format";
  if(r.containsKey("actions")==r.containsKey("sequence"))return "ai_sequence_source";
  if(r.containsKey("actions")&&!r["actions"].is<JsonArrayConst>())return "ai_sequence_source";
  if(r.containsKey("sequence")){auto s=r["sequence"].as<JsonObjectConst>();if(s.isNull())return "ai_sequence_source";if(auto e=argumentFields(s,"path,format"))return e;if(s["format"]!="jsonl"||!s["path"].is<const char*>()||r["transfer_id"].isNull())return "ai_sequence_source";}
  t.envelope.set(r);if(t.envelope.overflowed())return "memory_low";copy(t.operation,sizeof t.operation,r["operation_id"]|"");return nullptr;
 }
 const char*attach(Task&t){auto r=t.envelope.as<JsonObjectConst>();if(r.containsKey("transfer_id")){
   if(!r["transfer_id"].is<uint32_t>())return "ai_transfer_id";if(auto e=assets_.verified(r["transfer_id"],".",t.root,sizeof t.root))return e;t.transfer=r["transfer_id"];const char*relative;auto p=vfs_.resolve(t.root,relative);if(!p||!p->available())return "storage_unavailable";
   t.lease=new(std::nothrow)Vfs::Access(vfs_,false);if(!t.lease)return "memory_low";if(auto e=t.lease->add(t.root,p->caseInsensitive()))return e;if(!vfs_.pin(*p,t.id+0x71000000u))return "provider_lease_capacity";t.provider=p;
   if(r.containsKey("sequence")){if(auto e=assets_.verified(r["transfer_id"],r["sequence"]["path"],t.plan,sizeof t.plan))return e;vfs_.resolve(t.plan,relative);FileInfo info;if(auto e=p->stat(relative,info))return e;if(info.directory)return "ai_sequence_file";t.fileSize=info.size;t.filePlan=true;t.line=(uint8_t*)core_.resources().allocate(t.lineLimit+1);if(!t.line)return "memory_low";}
   JsonDocument a,o;a["id"]=r["transfer_id"];if(auto e=assets_.invoke("release",a.as<JsonObjectConst>(),o.to<JsonObject>()))return e;t.transfer=0;
  }
  return nullptr;
 }
 // Return a complete action or EOF; a transient bus wait preserves the partial line.
 const char*nextAction(Task&t,bool&end){end=false;t.action.clear();if(!t.filePlan){auto list=t.envelope["actions"].as<JsonArrayConst>();if(t.index>=list.size()){end=true;return nullptr;}t.action.set(list[t.index]);}
  else{const char*relative;vfs_.resolve(t.plan,relative);uint8_t bytes[256];bool lineReady=false;
   while(t.position<t.fileSize&&!lineReady){size_t got=0,n=std::min<uint64_t>(sizeof bytes,t.fileSize-t.position);auto e=t.provider->read(relative,t.position,bytes,n,got);if(e)return e;if(!got)return "ai_sequence_short_read";
    for(size_t i=0;i<got;++i){++t.position;if(bytes[i]=='\n'){if(t.lineSize){lineReady=true;break;}continue;}if(t.lineSize>=t.lineLimit)return "ai_action_limit";t.line[t.lineSize++]=bytes[i];}
   }
   if(!t.lineSize){end=true;return nullptr;}auto e=deserializeJson(t.action,t.line,t.lineSize,DeserializationOption::NestingLimit(16));t.lineSize=0;if(e)return "ai_sequence_json";
  }
  if(t.action.overflowed())return "memory_low";if(!t.action.is<JsonObject>()||measureJson(t.action)>t.lineLimit)return "ai_action_limit";return validateAction(t.action.as<JsonObjectConst>());
 }
 const char*expand(Task&t,JsonVariant v,unsigned depth=0){if(depth>16)return "ai_reference_depth";if(v.is<JsonArray>()){for(auto child:v.as<JsonArray>())if(auto e=expand(t,child,depth+1))return e;return nullptr;}if(!v.is<JsonObject>())return nullptr;auto object=v.as<JsonObject>();
  if(object.containsKey("$asset")){if(object.size()!=1||!object["$asset"].is<const char*>()||!*t.root)return "ai_asset_reference";const char*name=object["$asset"];char file[256];if(!*name||*name=='/'||!normalizePath(t.root,name,file,sizeof file,t.root))return "ai_asset_reference";v.set(file);return nullptr;}
  if(object.containsKey("$previous")){if(object.size()!=1||!object["$previous"].is<const char*>())return "ai_result_reference";const char*path=object["$previous"];JsonVariantConst value=t.previous.as<JsonVariantConst>();if(*path){char local[256];if(!copy(local,sizeof local,path))return "ai_result_reference";char*save=nullptr;for(char*k=strtok_r(local,"/",&save);k;k=strtok_r(nullptr,"/",&save)){if(!value.is<JsonObjectConst>()||!value.as<JsonObjectConst>().containsKey(k))return "ai_result_reference";value=value[k];}}v.set(value);return nullptr;}
  for(auto item:object)if(auto e=expand(t,item.value(),depth+1))return e;return nullptr;
 }
 void advance(Task&t,JsonVariantConst result){t.previous.set(result);if(t.previous.overflowed()){finish(t,"memory_low");return;}++t.index;t.appPending=false;t.recipient=0;t.action.clear();strcpy(t.state,"executing");notify(t,"step_completed");}
 uint32_t foreground(){JsonDocument o;if(core_.invoke("/sys/runtime","read",JsonObjectConst(),o.to<JsonObject>()))return 0;return o["loaded"]==true?o["owner"]|0u:0;}
 void execute(Task&t){bool end;auto error=nextAction(t,end);if(error){if(Core::containsMethod("spi_busy,resource_busy",error))return;finish(t,error);return;}if(end){finish(t);return;}
  if(auto e=expand(t,t.action.as<JsonVariant>())){finish(t,e);return;}if(t.action.overflowed()||measureJson(t.action)>t.lineLimit){finish(t,"ai_action_limit");return;}if(auto e=validateAction(t.action.as<JsonObjectConst>())){finish(t,e);return;}
  if(t.action["type"]=="application"){
   t.recipient=t.action["recipient"]=="foreground"?foreground():t.detached?0:t.origin;if(!t.recipient){finish(t,"ai_application_unavailable");return;}t.appPending=true;strcpy(t.state,"awaiting_application");notify(t,"application_action",t.recipient);
   if(t.action["wait"]=="queued"){JsonDocument queued;queued["queued"]=true;advance(t,queued.as<JsonVariantConst>());}return;
  }
  copy(t.effectPath,sizeof t.effectPath,t.action["path"]);copy(t.effectMethod,sizeof t.effectMethod,t.action["method"]);JsonDocument schema,out;core_.describe(t.effectPath,schema.to<JsonObject>());t.inCall=true;
  if(schema["async"].as<JsonObjectConst>().containsKey(t.effectMethod)){error=core_.request(t.effectPath,t.effectMethod,t.action["args"],out.to<JsonObject>(),0);if(!error)t.effect=out["completion_id"];}
  else error=core_.invoke(t.effectPath,t.effectMethod,t.action["args"],out.to<JsonObject>(),0);
  t.inCall=false;if(error){t.previous.set(out);finish(t,error);return;}if(t.effect)strcpy(t.state,"awaiting_system");else if(t.stop)finish(t,*t.error?t.error:"cancelled");else advance(t,out.as<JsonVariantConst>());
 }
 void cancelRemote(Task&t){JsonDocument a,o;a["id"]=t.remote;a["ok"]=false;a["result"]["error"]="cancelled";core_.invoke("/services/remote","complete",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner_);a.remove("ok");a.remove("result");core_.invoke("/services/remote","release",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner_);}
public:
 static constexpr const char*Methods="read,read_action,read_result,acknowledge,configure,submit,cancel,release";using Save=bool(*)(const char*,size_t);
 AgentService(Core&c,AssetTransfers&a,Vfs&v):core_(c),assets_(a),vfs_(v){owner_=core_.acquireOwner();config_["route"]="wishos.ai";config_["timeout_ms"]=900000;config_["max_action_bytes"]=2048;core_.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<AgentService*>(p);for(auto&t:s.tasks_){if(t.origin==owner)t.detached=true;if(t.appPending&&t.recipient==owner){t.recipient=0;}}});}
 ~AgentService(){core_.removeOwnerHook(this);for(auto&t:tasks_)cleanup(t);core_.releaseOwner(owner_);}
 bool restore(const char*s){JsonDocument d,o;if(deserializeJson(d,s))return false;JsonDocument a;a["settings"]=d;return !invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>());}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o,Save save=nullptr){const char*fields=!strcmp(m,"configure")?"settings,persist":!strcmp(m,"submit")?"input,request_id,timeout_ms":!strcmp(m,"acknowledge")?"id,step,ok,result":!strcmp(m,"read_result")?"id,offset,length":!strcmp(m,"read_action")?"id,step":"id";if(auto e=argumentFields(a,fields))return e;
  if(!strcmp(m,"configure")){if(a.containsKey("persist")&&!a["persist"].is<bool>())return "ai_configuration";auto settings=a["settings"].as<JsonObjectConst>();if(settings.isNull())return "ai_configuration";JsonDocument next;next.set(config_);for(auto f:settings){if(!Core::containsMethod("route,timeout_ms,max_action_bytes",f.key().c_str()))return "unknown_ai_setting";next[f.key()]=f.value();}if(!next["route"].is<const char*>()||!strlen(next["route"])||strlen(next["route"])>64||!next["timeout_ms"].is<uint32_t>()||next["timeout_ms"].as<uint32_t>()<100||next["timeout_ms"].as<uint32_t>()>86400000||!next["max_action_bytes"].is<uint32_t>()||next["max_action_bytes"].as<uint32_t>()<256||next["max_action_bytes"].as<uint32_t>()>MAX_WIRE-512)return "ai_configuration";char text[256];size_t n=serializeJson(next,text,sizeof text);if((a["persist"]|false)&&(!save||!save(text,n)))return "persistence_failed";swap(config_,next);o["configured"]=config_;return nullptr;}
  if(!strcmp(m,"submit")){if(!a["input"].is<JsonObjectConst>()||a.containsKey("request_id")&&!a["request_id"].is<const char*>())return "ai_request";if(a.containsKey("timeout_ms")&&!a["timeout_ms"].is<uint32_t>())return "ai_request_type";const char*key=a["request_id"]|"";uint32_t timeout=a["timeout_ms"]|config_["timeout_ms"].as<uint32_t>();if(strlen(key)>64||timeout<100||timeout>86400000)return "ai_request_range";
   Task*t=nullptr;for(auto&x:tasks_)if(!x.id){t=&x;break;}if(!t)for(auto&x:tasks_)if(x.detached&&terminal(x)){cleanup(x);x=Task();t=&x;break;}if(!t)return "ai_queue_full";JsonDocument args,reply;args["route"]=config_["route"];args["timeout_ms"]=timeout;if(*key)args["request_id"]=key;args["payload"]["format"]="wishos-ai/2";args["payload"]["input"]=a["input"];if(auto e=core_.invoke("/services/remote","submit",args.as<JsonObjectConst>(),reply.to<JsonObject>(),owner_))return e;
   for(auto&old:tasks_)if(old.id&&old.remote==reply["id"].as<uint32_t>()){if(old.origin!=core_.callerOwner())return "ai_request_owned_by_other";o["id"]=old.id;o["state"]=old.state;return nullptr;}
   t->id=next_++;t->origin=core_.callerOwner();t->remote=reply["id"];t->deadline=core_.now()+timeout;t->lineLimit=config_["max_action_bytes"];copy(t->key,sizeof t->key,reply["request_id"]|"");strcpy(t->state,"queued");o["id"]=t->id;o["request_id"]=t->key;o["state"]=t->state;return nullptr;
  }
  if(!strcmp(m,"read")&&!a.containsKey("id")){o["configured"]=config_;auto list=o["tasks"].to<JsonArray>();for(auto&t:tasks_)if(t.id&&(!core_.callerOwner()||t.origin==core_.callerOwner()||t.recipient==core_.callerOwner())){auto x=list.add<JsonObject>();x["id"]=t.id;x["state"]=t.state;x["step"]=t.index;x["steps"]=t.count;x["detached"]=t.detached;}return nullptr;}
  auto*t=find(a["id"]|0u);if(!t)return "ai_request_not_found";if(core_.callerOwner()&&t->origin!=core_.callerOwner()&&t->recipient!=core_.callerOwner())return "ai_request_owned_by_other";
  if(!strcmp(m,"read_action")){if(!t->appPending||a["step"]!=t->index)return "ai_action_not_pending";o["id"]=t->id;o["step"]=t->index;o["action"]=t->action;return nullptr;}
  if(!strcmp(m,"acknowledge")){if(!t->appPending||a["step"]!=t->index)return "ai_action_not_pending";if(!t->recipient||core_.callerOwner()!=t->recipient)return "ai_action_owned_by_other";if(!a["ok"].is<bool>()||measureJson(a["result"])>MAX_WIRE-512)return "ai_action_result";if(a["ok"]==false){t->previous.set(a["result"]);finish(*t,"ai_application_failed");}else advance(*t,a["result"]);return nullptr;}
  if(!strcmp(m,"read_result")){for(auto key:{"offset","length"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "result_range";uint32_t at=a["offset"]|0u,n=a["length"]|512u;if(!n||n>512)return "result_range";struct Slice{size_t at,length,position=0,used=0;uint8_t bytes[512];size_t write(uint8_t b){if(position>=at&&used<length)bytes[used++]=b;++position;return 1;}size_t write(const uint8_t*p,size_t size){for(size_t i=0;i<size;++i)write(p[i]);return size;}}slice{at,n};serializeJson(t->previous,slice);if(at>slice.position)return "result_range";char encoded[1025];hex(slice.bytes,slice.used,encoded);o["hex"]=encoded;o["next_offset"]=at+slice.used;o["finished"]=at+slice.used==slice.position;return nullptr;}
  if(!strcmp(m,"cancel")){if(terminal(*t))return "ai_request_finished";t->stop=true;copy(t->error,sizeof t->error,"cancelled");if(!strcmp(t->state,"queued"))cancelRemote(*t);if(!t->effect&&!t->inCall)finish(*t,"cancelled");}
  else if(!strcmp(m,"release")){if(!terminal(*t))return "ai_request_pending";cleanup(*t);*t=Task();return nullptr;}
  else if(strcmp(m,"read"))return "unsupported_method";
  o["id"]=t->id;o["state"]=t->state;o["step"]=t->index;o["steps"]=t->count;o["error"]=t->error;o["cancel_requested"]=t->stop;o["result_bytes"]=measureJson(t->previous);return nullptr;
 }
 void poll(){for(auto&t:tasks_)if(t.id&&!terminal(t)){
  if(core_.now()>=t.deadline&&!t.stop){t.stop=true;copy(t.error,sizeof t.error,"deadline_exceeded");if(!strcmp(t.state,"queued"))cancelRemote(t);}
  if(t.stop&&!t.effect){finish(t,*t.error?t.error:"cancelled");continue;}
  if(t.follows){if(auto r=receipt(t.operation)){t.follows=0;t.index=r->completed;finish(t,*r->error?r->error:nullptr);}else if(auto leader=find(t.follows)){if(terminal(*leader)){t.index=leader->index;finish(t,*leader->error?leader->error:nullptr);}}else finish(t,"ai_operation_receipt_missing");continue;}
  if(t.effect){JsonDocument done;auto e=core_.completions().read(t.effect,0,done.to<JsonObject>());if(e){t.effect=0;finish(t,e);continue;}if(done["ready"]!=true)continue;core_.completions().release(t.effect,0);t.effect=0;t.previous.set(done["data"]);if(t.stop){finish(t,*t.error?t.error:"cancelled");continue;}JsonDocument schema;core_.describe(t.effectPath,schema.to<JsonObject>());bool success=false;for(auto name:schema["async"][t.effectMethod]["success_events"].as<JsonArrayConst>())if(name==done["event"])success=true;if(!success||done["data"]["ok"]==false){finish(t,done["data"]["error"]|"ai_system_action_failed");continue;}advance(t,done["data"]);continue;}
  if(t.appPending){if(!t.recipient)finish(t,"ai_application_unavailable");continue;}
  if(!strcmp(t.state,"validating")){bool end;auto e=nextAction(t,end);if(e){if(!Core::containsMethod("spi_busy,resource_busy",e))finish(t,e);continue;}if(end){t.count=t.index;t.index=0;t.position=0;t.lineSize=0;t.action.clear();strcpy(t.state,"executing");notify(t,"result_ready");}else ++t.index;continue;}
  if(!strcmp(t.state,"executing")){execute(t);continue;}
  if(strcmp(t.state,"queued"))continue;JsonDocument args,reply;args["id"]=t.remote;auto e=core_.invoke("/services/remote","read",args.as<JsonObjectConst>(),reply.to<JsonObject>(),owner_);if(e){finish(t,e);continue;}if(reply["state"]=="queued")continue;
  if(reply["state"]=="failed"){t.previous.set(reply["result"]);finish(t,reply["result"]["error"]|"ai_remote_failed");}else if(auto error=envelope(t,reply["result"]))finish(t,error);else{
   auto prior=receipt(t.operation);Task*leader=nullptr;if(*t.operation&&!prior)for(auto&other:tasks_)if(&other!=&t&&other.id&&!other.follows&&!strcmp(other.operation,t.operation)){leader=&other;break;}
   if(prior||leader){char expected[65],actual[65];digest(t.envelope.as<JsonVariantConst>(),actual);if(prior)copy(expected,sizeof expected,prior->hash);else digest(leader->envelope.as<JsonVariantConst>(),expected);if(strcmp(actual,expected))finish(t,"ai_operation_conflict");else if(prior){t.index=prior->completed;finish(t,*prior->error?prior->error:nullptr);}else{t.follows=leader->id;strcpy(t.state,"following");}}
   else{t.ownsOperation=true;if(auto error=attach(t))finish(t,error);else strcpy(t.state,"validating");}
  }
  reply.clear();core_.invoke("/services/remote","release",args.as<JsonObjectConst>(),reply.to<JsonObject>(),owner_);
 }}
 void describe(JsonObject o)const{o["scope"]="system_ai_broker";o["request_format"]="wishos-ai/2";o["result_format"]="wishos-result/2";o["action_types"]="application,system";o["action_order"]="sequential_with_declared_service_completions";o["application_recipients"]="origin,foreground";o["application_wait"]="processed_by_acknowledge_or_explicit_queued";o["sequence_sources"]="inline_actions_or_verified_jsonl_file";o["argument_references"]="$asset relative path; $previous slash-separated object path";o["pending_capacity"]=4;o["inline_result_limit_bytes"]=1536;o["action_limit_bytes"]=config_["max_action_bytes"];o["sequence_count_limit"]=nullptr;o["lifetime"]="current_boot";o["failure"]="stop_without_rollback_or_automatic_replay";o["cancellation"]="stop_after_inflight_native_completion";o["duplicate_operation_receipts"]=8;asyncContract(o,"submit","/services/ai")["id"]="id";}
};
}
