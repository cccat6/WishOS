#pragma once
#include "LuaRuntime.h"
#include "Policy.h"
#include <atomic>
namespace notesos {
// Isolated compute jobs. Only the main loop accesses the resource registry and VFS.
class Workers {
public:
 struct Backend {unsigned cores=1;bool(*launch)(void(*)(void*),void*,unsigned)=nullptr;void(*yield)(void*)=nullptr;bool(*admit)(size_t)=nullptr;};
private:
 struct Job{uint32_t id=0,owner=0;bool orphan=false;unsigned core=0,budget=0;LuaRuntime*vm=nullptr;char*source=nullptr;size_t size=0;JsonDocument input,result;std::atomic<bool>cancel{false},started{false},done{false};bool ok=false,notified=false;const char*failure=nullptr;void(*yield)(void*)=nullptr;~Job(){delete vm;free(source);}};
 Core&core_;Vfs&vfs_;Backend backend_;bool registered_=false;Job*jobs_[2]{};uint32_t next_=1;
 static void execute(void*arg){auto&j=*(Job*)arg;j.started=true;j.vm->isolate(j.yield,[](void*p){return ((Job*)p)->cancel.load();},&j);
   j.ok=!j.cancel.load()&&j.vm->load(j.source,j.size);if(j.ok)j.ok=j.vm->compute(j.input.as<JsonVariantConst>(),j.result.to<JsonVariant>());if(j.result.overflowed()){j.ok=false;j.failure="result_memory_low";}j.done.store(true,std::memory_order_release);
 }
public:
 Workers(Core&c,Vfs&v,Backend b):core_(c),vfs_(v),backend_(b){registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){((Workers*)p)->releaseOwner(owner);});}
 ~Workers(){core_.removeOwnerHook(this);for(auto j:jobs_){if(j&&!j->done.load())std::abort();delete j;}}
 bool active()const{for(auto j:jobs_)if(j&&!j->done.load())return true;return false;}
 void releaseOwner(uint32_t owner){for(auto j:jobs_)if(j&&j->owner==owner){j->cancel=true;j->orphan=true;}}
 void poll(){for(auto&j:jobs_)if(j&&j->orphan&&j->done.load(std::memory_order_acquire)){delete j;j=nullptr;}for(auto j:jobs_)if(j&&j->done.load(std::memory_order_acquire)&&!j->notified){j->notified=true;JsonDocument d;d["id"]=j->id;d["owner"]=j->owner;d["ok"]=j->ok;d["cancel_requested"]=j->cancel.load();if(!j->ok)d["error"]=j->failure?j->failure:*j->vm->error()?j->vm->error():"cancelled";core_.emit("/sys/workers",j->ok?"completed":"failed",d.as<JsonObjectConst>());}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out){
   if(!strcmp(m,"submit")){
     for(auto f:a)if(!Policies::choice("source,file,input,core,memory_bytes,instruction_budget",f.key().c_str()))return "unknown_worker_field";
     if(!registered_)return "owner_hook_capacity";
     if(!backend_.launch)return "worker_backend_unavailable";
     unsigned affinity=backend_.cores-1,budget=a["memory_bytes"]|32768u,steps=a["instruction_budget"]|200000u;
     if(a.containsKey("core")){if(a["core"].is<const char*>()&&!strcmp(a["core"],"auto")){}else if(a["core"].is<unsigned>())affinity=a["core"];else return "worker_core";}
     if(affinity>=backend_.cores)return "worker_core_unavailable";
     if((a.containsKey("memory_bytes")&&!a["memory_bytes"].is<unsigned>())||(a.containsKey("instruction_budget")&&!a["instruction_budget"].is<unsigned>())||budget<16384||budget>131072||steps<1000||steps>10000000)return "worker_budget_range";
     if(measureJson(a["input"])>4096)return "worker_input_limit";
     Job**slot=nullptr;for(auto&j:jobs_)if(!j){slot=&j;break;}if(!slot)return "worker_capacity";
     size_t reservation=budget+24576;
     if(backend_.admit&&!backend_.admit(reservation))return "worker_memory_budget";
     if(!next_)return "worker_id_exhausted";
     auto j=new(std::nothrow)Job;if(!j)return "memory_low";
     if(a["file"].is<const char*>()){
       if(a.containsKey("source")){delete j;return "one_script_source_required";}char full[256];const char*rel;
       if(!normalizePath("/",a["file"],full,sizeof full)){delete j;return "script_path";}auto p=vfs_.resolve(full,rel);if(!p){delete j;return "unknown_script_mount";}
       constexpr uint32_t lease=0xffffffe8u;if(!vfs_.lockRead(full,lease)){delete j;return "file_lease_capacity";}
       FileInfo info;const char*e=p->stat(rel,info);if(!e&&(info.directory||!info.size||info.size>8192))e="script_size";
       if(!e){j->size=info.size;j->source=(char*)malloc(j->size+1);if(!j->source)e="memory_low";else{size_t got=0;e=p->read(rel,0,(uint8_t*)j->source,j->size,got);if(!e&&got!=j->size)e="script_short_read";j->source[j->size]=0;}}
       vfs_.release(lease);if(e){delete j;return e;}
     }else{const char*s=a["source"]|"";j->size=strlen(s);if(!j->size||j->size>8192){delete j;return "script_size";}j->source=strdup(s);if(!j->source){delete j;return "memory_low";}}
     j->vm=new(std::nothrow)LuaRuntime(core_,budget,steps);if(!j->vm){delete j;return "memory_low";}if(!j->vm->admitted()){delete j;return "system_memory_budget";}if(!j->input.set(a["input"])){delete j;return "memory_low";}j->id=next_++;j->owner=core_.callerOwner();j->core=affinity;j->budget=budget;j->yield=backend_.yield;
     if(!backend_.launch(execute,j,affinity)){j->done=true;delete j;return "worker_launch_failed";}*slot=j;out["id"]=j->id;out["status"]="accepted";out["core"]=affinity;return nullptr;
   }
   if(!strcmp(m,"read")&&!a.containsKey("id")){out["active"]=active();out["cores"]=backend_.cores;out["capacity"]=2;out["scheduling"]="background_preemptible";out["hardware_access"]="main_application_resource_calls";auto jobs=out["jobs"].to<JsonArray>();for(auto j:jobs_)if(j&&(!core_.callerOwner()||j->owner==core_.callerOwner())){auto r=jobs.add<JsonObject>();r["id"]=j->id;r["core"]=j->core;r["state"]=j->done.load()?"finished":j->started.load()?"running":"queued";}return nullptr;}
   Job**slot=nullptr;for(auto&j:jobs_)if(j&&j->id==(a["id"]|0u)){slot=&j;break;}if(!slot)return "worker_not_found";auto j=*slot;if(core_.callerOwner()&&j->owner!=core_.callerOwner())return "worker_owned_by_other";bool done=j->done.load(std::memory_order_acquire);
   if(!strcmp(m,"cancel"))j->cancel=true;
   else if(!strcmp(m,"release")){if(!done)return "worker_running";delete j;*slot=nullptr;return nullptr;}
   else if(strcmp(m,"read"))return "unsupported_method";
   out["id"]=j->id;out["owner"]=j->owner;out["core"]=j->core;out["state"]=done?(j->ok?"completed":"failed"):j->started.load()?"running":"queued";out["cancel_requested"]=j->cancel.load();
   if(done){out["ok"]=j->ok;if(j->ok)out["result"]=j->result;else out["error"]=j->failure?j->failure:*j->vm->error()?j->vm->error():"cancelled";j->vm->describe(out["runtime"].to<JsonObject>());}return nullptr;
 }
};
}
