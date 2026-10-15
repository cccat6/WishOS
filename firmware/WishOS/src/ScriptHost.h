#pragma once
#include "LuaRuntime.h"
#include "LuaSyntax.h"
#include "Policy.h"
namespace notesos {
// Portable application host: file providers, resource calls and the shared allocation policy.
class ScriptHost {
 Core&core_;Vfs&vfs_;LuaRuntime*vm_=nullptr;char file_[256]{};Vfs::Access*lease_=nullptr;uint32_t pin_=0;
 void releaseFiles(){delete lease_;lease_=nullptr;if(pin_)vfs_.unpin(pin_);pin_=0;}
 static const char*source(void*ctx,const char*file,char*&text,size_t&size){
   auto&s=*(ScriptHost*)ctx;const char*relative;auto p=s.vfs_.resolve(file,relative);if(!p)return "unknown_script_mount";
   constexpr uint32_t lease=0xffffffe7u;if(!s.vfs_.lockRead(file,lease))return "file_lease_capacity";
   FileInfo info;const char*error=p->stat(relative,info);text=nullptr;size=0;
   if(!error&&(info.directory||!info.size||info.size>8192))error="script_size";
   if(!error){size=info.size;text=(char*)malloc(size+1);if(!text)error="memory_low";else{size_t got=0;error=p->read(relative,0,(uint8_t*)text,size,got);if(!error&&got!=size)error="script_short_read";text[size]=0;}}
   s.vfs_.release(lease);return error;
 }
 void notify(const char*name,const char*error=""){JsonDocument d;d["file"]=file_;if(vm_)d["owner"]=owner();if(*error)d["error"]=error;core_.emit("/sys/runtime",name,d.as<JsonObjectConst>());}
 uint32_t owner()const{JsonDocument d;if(vm_)vm_->describe(d.to<JsonObject>());return d["owner"]|0u;}
public:
 static void describe(Core&core,JsonObject o){runtimeContract(core,o);}
 ScriptHost(Core&c,Vfs&v):core_(c),vfs_(v){}
 ~ScriptHost(){delete vm_;releaseFiles();}
 void tick(uint64_t){if(vm_){bool before=*vm_->error();vm_->flushCallbacks();if(!before&&*vm_->error())notify("fault",vm_->error());}}
 bool event(const Event&e){if(!vm_)return false;bool before=*vm_->error();bool consumed=vm_->event(e);if(!before&&*vm_->error())notify("fault",vm_->error());return consumed;}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out){
   if(!strcmp(m,"read")){out["loaded"]=vm_!=nullptr;out["file"]=file_;if(vm_)vm_->describe(out);out["default_vm_memory_bytes"]=core_.resources().defaultVmLimit();out["memory_policy_limit_bytes"]=core_.resources().limit();out["source_limit_bytes"]=LuaSyntax::SourceLimit;out["module_limit"]=8;out["json_decode_limit_bytes"]=4096;out["native_driver_loading"]=false;return nullptr;}
   if(!strcmp(m,"validate")){if(auto e=argumentFields(a,"file,source,memory_bytes"))return e;if(a.containsKey("file")==a.containsKey("source"))return "one_script_source_required";size_t budget=a["memory_bytes"]|uint32_t(core_.resources().defaultVmLimit());if(a.containsKey("memory_bytes")&&(!a["memory_bytes"].is<uint32_t>()||budget<16384||budget>core_.resources().limit()))return "runtime_budget_range";if(a.containsKey("source")){if(!a["source"].is<const char*>())return "script_source_type";auto s=a["source"].as<JsonString>();return LuaSyntax::compile(core_,s.c_str(),s.size(),out,budget);}if(!a["file"].is<const char*>())return "script_source_type";char full[256];if(!normalizePath("/",a["file"],full,sizeof full))return "script_path";return LuaSyntax::file(core_,vfs_,full,out,budget);}
   if(vm_&&vm_->executing())return "runtime_executing";
   if(!strcmp(m,"stop")){if(vm_){bool clean=vm_->stop();out["cleanup_ok"]=clean;if(!clean)out["cleanup_error"]=vm_->error();notify("stopped");delete vm_;vm_=nullptr;}releaseFiles();file_[0]=0;return nullptr;}
   if(strcmp(m,"load")&&strcmp(m,"restart"))return "unsupported_method";
   for(auto f:a)if(!Policies::choice("file,source,root,memory_bytes,instruction_budget,system_call_budget",f.key().c_str()))return "unknown_runtime_field";
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
   Vfs::Access*lease=nullptr;uint32_t pin=0;if(a.containsKey("root")){char root[256];JsonDocument state;candidate->describe(state.to<JsonObject>());const char*relative;auto provider=vfs_.resolve(file,relative);const char*error=nullptr;if(!a["root"].is<const char*>()||!normalizePath("/",a["root"],root,sizeof root)||!pathWithin(root,file)||!provider)error="script_root";if(!error){lease=new(std::nothrow)Vfs::Access(vfs_,false);if(!lease)error="memory_low";else error=lease->add(root,provider->caseInsensitive());}if(!error){pin=state["owner"];if(!vfs_.pin(*provider,pin)){pin=0;error="provider_lease_capacity";}}if(error){delete lease;delete candidate;free(text);return error;}}
   bool loaded=candidate->load(text,size);free(text);if(loaded)loaded=candidate->prepare();if(!loaded){out["detail"]=candidate->error();candidate->describe(out["candidate"].to<JsonObject>());delete candidate;delete lease;if(pin)vfs_.unpin(pin);return "script_load_failed";}
   // Preparation is atomic. on_start is side-effectful and cannot be rolled back.

   if(vm_){out["previous_cleanup_ok"]=vm_->stop();delete vm_;}releaseFiles();vm_=candidate;lease_=lease;pin_=pin;copy(file_,sizeof file_,file);
   bool started=vm_->start();vm_->describe(out);out["status"]=started?"running":"fault";notify(started?"started":"fault",started?"":vm_->error());return started?nullptr:"script_start_failed";
 }
};
}
