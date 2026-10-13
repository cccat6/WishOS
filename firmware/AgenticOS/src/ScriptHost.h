#pragma once
#include "LuaRuntime.h"
#include "Policy.h"
namespace notesos {
// Portable application host: file providers, resource calls and the shared allocation policy.
class ScriptHost {
 Core&core_;Vfs&vfs_;LuaRuntime*vm_=nullptr;char file_[256]{};JsonDocument startup_,bootStartup_;
 uint32_t revision_=1;bool restoreValid_=true,persisted_=true,bootPending_=false;uint64_t bootDue_=0;
 char bootError_[80]{};
 static const char*source(void*ctx,const char*file,char*&text,size_t&size){
   auto&s=*(ScriptHost*)ctx;const char*relative;auto p=s.vfs_.resolve(file,relative);if(!p)return "unknown_script_mount";
   constexpr uint32_t lease=0xffffffe7u;if(!s.vfs_.lockRead(file,lease))return "file_lease_capacity";
   FileInfo info;const char*error=p->stat(relative,info);text=nullptr;size=0;
   if(!error&&(info.directory||!info.size||info.size>8192))error="script_size";
   if(!error){size=info.size;text=(char*)malloc(size+1);if(!text)error="memory_low";else{size_t got=0;error=p->read(relative,0,(uint8_t*)text,size,got);if(!error&&got!=size)error="script_short_read";text[size]=0;}}
   s.vfs_.release(lease);return error;
 }
 const char*validate(JsonObjectConst a)const{
   for(auto f:a)if(!Policies::choice("auto_start,file,memory_bytes,instruction_budget,system_call_budget,delay_ms,prepare",f.key().c_str()))return "unknown_startup_field";
   if(!a["auto_start"].is<bool>()||!a["file"].is<const char*>())return "startup_type";
   const char*file=a["file"];char normal[256];if(*file&&(!normalizePath("/",file,normal,sizeof normal)||strcmp(normal,file)))return "script_path";
   if(a["auto_start"].as<bool>()&&!*file)return "startup_file_required";
   if((!a["memory_bytes"].isNull()&&(!a["memory_bytes"].is<uint32_t>()||a["memory_bytes"].as<uint32_t>()<16384||a["memory_bytes"].as<uint32_t>()>core_.resources().limit()))||!a["instruction_budget"].is<uint32_t>()||a["instruction_budget"].as<uint32_t>()<1000||a["instruction_budget"].as<uint32_t>()>200000||!a["delay_ms"].is<uint32_t>()||a["delay_ms"].as<uint32_t>()>60000)return "runtime_budget_range";
   if(a.containsKey("system_call_budget")&&(!a["system_call_budget"].is<uint32_t>()||a["system_call_budget"].as<uint32_t>()<1||a["system_call_budget"].as<uint32_t>()>256))return "runtime_budget_range";
   if(a.containsKey("prepare")){auto steps=a["prepare"].as<JsonArrayConst>();if(steps.isNull()||steps.size()>4||measureJson(steps)>512)return "startup_preparation_limit";for(auto s:steps){if(!s.is<JsonObjectConst>())return "startup_preparation_type";for(auto f:s.as<JsonObjectConst>())if(!Policies::choice("path,method,args",f.key().c_str()))return "startup_preparation_field";if(!s["path"].is<const char*>()||s["path"].as<const char*>()[0]!='/'||strlen(s["path"].as<const char*>())>=48||!s["method"].is<const char*>()||!strlen(s["method"].as<const char*>())||strlen(s["method"].as<const char*>())>=32||!s["args"].is<JsonObjectConst>())return "startup_preparation_type";}}
   return nullptr;
 }
 const char*validateProvider(JsonObjectConst a)const{if(a["auto_start"]|false){const char*relative;auto p=vfs_.resolve(a["file"],relative);if(!p||!p->persistent)return "startup_requires_persistent_provider";}return nullptr;}
 void notify(const char*name,const char*error=""){JsonDocument d;d["file"]=file_;if(vm_)d["owner"]=owner();if(*error)d["error"]=error;core_.emit("/sys/runtime",name,d.as<JsonObjectConst>());}
 uint32_t owner()const{JsonDocument d;if(vm_)vm_->describe(d.to<JsonObject>());return d["owner"]|0u;}
public:
 using Persist=bool(*)(const char*,size_t);
 ScriptHost(Core&c,Vfs&v):core_(c),vfs_(v){startup_["auto_start"]=false;startup_["file"]="";startup_["memory_bytes"]=nullptr;startup_["instruction_budget"]=20000;startup_["system_call_budget"]=32;startup_["delay_ms"]=1000;}
 ~ScriptHost(){delete vm_;}
 bool restore(const char*text){JsonDocument d;if(deserializeJson(d,text)||validate(d.as<JsonObjectConst>())||validateProvider(d.as<JsonObjectConst>())){restoreValid_=false;return false;}swap(startup_,d);restoreValid_=true;persisted_=true;return true;}
 void scheduleBoot(uint64_t now){bootStartup_.set(startup_);bootPending_=!bootStartup_.overflowed()&&(startup_["auto_start"]|false);bootDue_=now+(startup_["delay_ms"]|1000u);}
 void tick(uint64_t now){if(!bootPending_||now<bootDue_)return;bootPending_=false;JsonDocument boot=std::move(bootStartup_);JsonDocument args,out;for(auto s:boot["prepare"].as<JsonArrayConst>()){if(auto error=core_.invoke(s["path"],s["method"],s["args"].as<JsonObjectConst>(),out.to<JsonObject>())){copy(bootError_,sizeof bootError_,error);notify("boot_failed",error);return;}}for(auto key:{"file","memory_bytes","instruction_budget","system_call_budget"})if(!boot[key].isNull())args[key]=boot[key];auto error=invoke("load",args.as<JsonObjectConst>(),out.to<JsonObject>());if(error){copy(bootError_,sizeof bootError_,error);notify("boot_failed",error);}}
 bool event(const Event&e){if(!vm_)return false;bool before=*vm_->error();bool consumed=vm_->event(e);if(!before&&*vm_->error())notify("fault",vm_->error());return consumed;}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out,Persist persist=nullptr){
   if(!strcmp(m,"read")){out["loaded"]=vm_!=nullptr;out["file"]=file_;if(vm_)vm_->describe(out);out["default_vm_memory_bytes"]=core_.resources().defaultVmLimit();out["memory_policy_limit_bytes"]=core_.resources().limit();out["source_limit_bytes"]=8192;out["module_limit"]=8;out["native_driver_loading"]=false;out["configured_startup"]=startup_;out["configuration_revision"]=revision_;out["configuration_persisted"]=persisted_;out["restore_valid"]=restoreValid_;out["boot_pending"]=bootPending_;out["boot_error"]=bootError_;return nullptr;}
   if(vm_&&vm_->executing())return "runtime_executing";
   if(!strcmp(m,"configure")){
     for(auto f:a)if(!Policies::choice("startup,persist,expected_revision",f.key().c_str()))return "unknown_runtime_field";
     if(!a["startup"].is<JsonObjectConst>()||(a.containsKey("persist")&&!a["persist"].is<bool>()))return "runtime_config_type";
     if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
     JsonDocument next;next.set(startup_);for(auto f:a["startup"].as<JsonObjectConst>())next[f.key()]=f.value();if(next.overflowed())return "memory_low";if(auto error=validate(next.as<JsonObjectConst>()))return error;
     if(auto error=validateProvider(next.as<JsonObjectConst>()))return error;
     if(a["persist"]|false){char bytes[1024];size_t n=measureJson(next);if(n>=sizeof bytes)return "startup_limit";serializeJson(next,bytes,sizeof bytes);if(!persist||!persist(bytes,n))return "persistence_failed";}
     swap(startup_,next);++revision_;persisted_=a["persist"]|false;if(!(startup_["auto_start"]|false))bootPending_=false;
     out["revision"]=revision_;out["apply_time"]=persisted_?"next_boot":"requires_persistence";return nullptr;
   }
   if(!strcmp(m,"stop")){bootPending_=false;bootStartup_.clear();if(vm_){bool clean=vm_->stop();out["cleanup_ok"]=clean;if(!clean)out["cleanup_error"]=vm_->error();notify("stopped");delete vm_;vm_=nullptr;}file_[0]=0;return nullptr;}
   if(strcmp(m,"load")&&strcmp(m,"restart"))return "unsupported_method";
   for(auto f:a)if(!Policies::choice("file,source,memory_bytes,instruction_budget,system_call_budget",f.key().c_str()))return "unknown_runtime_field";
   for(auto key:{"file","source"})if(a.containsKey(key)&&!a[key].is<const char*>())return "script_source_type";
   unsigned budget=a["memory_bytes"]|uint32_t(core_.resources().defaultVmLimit()),steps=a["instruction_budget"]|20000u,calls=a["system_call_budget"]|32u;
   if((a.containsKey("system_call_budget")&&!a["system_call_budget"].is<uint32_t>())||calls<1||calls>256)return "runtime_budget_range";
   if((a.containsKey("memory_bytes")&&!a["memory_bytes"].isNull()&&!a["memory_bytes"].is<uint32_t>())||(a.containsKey("instruction_budget")&&!a["instruction_budget"].is<uint32_t>())||budget<16384||budget>core_.resources().limit()||steps<1000||steps>200000)return "runtime_budget_range";
   if(a.containsKey("file")&&a.containsKey("source"))return "one_script_source_required";
   char file[256]{};const char*path=a["file"]|"";if(!strcmp(m,"restart")&&!*path&&!a.containsKey("source"))path=file_;
   char*text=nullptr;size_t size=0;
   if(*path){if(!normalizePath("/",path,file,sizeof file))return "script_path";if(auto e=source(this,file,text,size)){free(text);return e;}}
   else{if(!a["source"].is<const char*>())return "script_source_required";auto value=a["source"].as<JsonString>();size=value.size();if(!size||size>8192||strlen(value.c_str())!=size)return "script_size";text=strdup(value.c_str());if(!text)return "memory_low";}
   auto candidate=new(std::nothrow)LuaRuntime(core_,budget,steps,calls);if(!candidate){free(text);return "memory_low";}candidate->fileContext(file,source,this);
   bool loaded=candidate->load(text,size);free(text);if(loaded)loaded=candidate->prepare();if(!loaded){out["detail"]=candidate->error();candidate->describe(out["candidate"].to<JsonObject>());delete candidate;return "script_load_failed";}
   // Preparation is atomic. on_start is side-effectful and cannot be rolled back.
   bootPending_=false;bootStartup_.clear();bootError_[0]=0;
   if(vm_){out["previous_cleanup_ok"]=vm_->stop();delete vm_;}vm_=candidate;copy(file_,sizeof file_,file);
   bool started=vm_->start();vm_->describe(out);out["status"]=started?"running":"fault";notify(started?"started":"fault",started?"":vm_->error());return started?nullptr:"script_start_failed";
 }
};
}
