#pragma once
#include "ScriptHost.h"
namespace notesos {
// Installed packages are references to files, not resident VMs. The manager owns
// activation/lifecycle; the launcher and voice entry are ordinary applications.
class Applications {
 Core&core_;Vfs&vfs_;ScriptHost&host_;JsonDocument catalog_,defaults_,activeArguments_;
 struct Pending {char id[49]{},path[256]{},fallbackFrom[49]{},fallbackError[96]{},preparationSource[48]{},preparationMethod[32]{};uint32_t operation=0,owner=0,preparation=0;bool home=false,voice=false,released=false,prepared=false;JsonDocument manifest,arguments;}pending_,followup_;bool loading_=false;
 uint32_t next_=1,revision_=1,systemOwner_=0;char activeId_[49]{},activeEntry_[256]{},lastError_[96]{};
 const char*factoryLauncher_;const char*factoryVoice_;size_t catalogBytes_;bool persisted_=true,restoreValid_=true;
 static bool identifier(const char*s){if(!s||!*s||strlen(s)>48)return false;for(auto p=s;*p;++p)if(!isalnum((unsigned char)*p)&&*p!='-'&&*p!='_'&&*p!='.')return false;return true;}
 JsonObjectConst entry(const char*id)const{if(!id)return {};for(auto e:catalog_["applications"].as<JsonArrayConst>())if(e["id"]==id)return e;for(auto e:defaults_.as<JsonArrayConst>())if(e["id"]==id)return e;return {};}
 const char*document(const char*file,JsonDocument&out){const char*rel;auto provider=vfs_.resolve(file,rel);if(!provider||!provider->available())return "application_storage_unavailable";Vfs::Access lease(vfs_,false);if(auto e=lease.add(file,provider->caseInsensitive()))return e;FileInfo info;if(auto e=provider->stat(rel,info))return e;if(info.directory||!info.size||info.size>4096)return "application_manifest_size";char*text=(char*)core_.resources().allocate(info.size);if(!text)return "application_memory_low";size_t got=0;const char*error=provider->read(rel,0,(uint8_t*)text,info.size,got);if(!error&&(got!=info.size||deserializeJson(out,text,got)))error="application_manifest_json";core_.resources().freeAllocation(text);return error;
 }
 const char*manifest(const char*file,JsonDocument&out,bool checkCapabilities=true){if(auto e=document(file,out))return e;if(out["format"]!="wishos-app/1"||!out["id"].is<const char*>()||!identifier(out["id"])||!out["title"].is<const char*>()||!out["entry"].is<const char*>())return "application_manifest";
  char base[256],resolved[256];if(!copy(base,sizeof base,file))return "application_path";auto slash=strrchr(base,'/');if(!slash)return "application_path";if(slash==base)slash[1]=0;else *slash=0;
  if(!normalizePath(base,out["entry"],resolved,sizeof resolved))return "application_entry_path";out["entry"]=resolved;
  if(!out["requires"].isNull()){if(!out["requires"].is<JsonArrayConst>())return "application_requirements";for(auto r:out["requires"].as<JsonArrayConst>()){const char*path=r["path"]|"";auto device=core_.find(path);if(checkCapabilities&&!device)return "application_capability_missing";if(!r["methods"].is<JsonArrayConst>())return "application_requirements";for(auto m:r["methods"].as<JsonArrayConst>())if(!m.is<const char*>()||(checkCapabilities&&!core_.supports(*device,m)))return "application_method_missing";}}
  return out.overflowed()?"application_memory_low":nullptr;
 }
 void finish(const char*error=nullptr){lastError_[0]=0;if(error)copy(lastError_,sizeof lastError_,error);else if(*pending_.fallbackError)copy(lastError_,sizeof lastError_,pending_.fallbackError);JsonDocument d;d["operation_id"]=pending_.operation;d["application_id"]=pending_.id;if(*pending_.fallbackFrom){d["requested_application_id"]=pending_.fallbackFrom;d["fallback"]=true;d["fallback_error"]=pending_.fallbackError;}d["ok"]=!error;d["error"]=error?error:"";core_.emit("/sys/apps",error?"failed":"launched",d.as<JsonObjectConst>(),pending_.owner);if(pending_.preparation)core_.completions().release(pending_.preparation,systemOwner_);pending_=std::move(followup_);followup_=Pending();}
 void failedActivation(const char*error){const char*fallback=pending_.home?factoryLauncher_:pending_.voice?factoryVoice_:"";if(*fallback&&strcmp(pending_.id,fallback)){if(pending_.preparation)core_.completions().release(pending_.preparation,systemOwner_);pending_.preparation=0;pending_.prepared=false;pending_.manifest.clear();copy(pending_.fallbackFrom,sizeof pending_.fallbackFrom,pending_.id);copy(pending_.fallbackError,sizeof pending_.fallbackError,error);copy(pending_.id,sizeof pending_.id,fallback);copy(pending_.path,sizeof pending_.path,entry(fallback)["manifest"]|"");copy(lastError_,sizeof lastError_,error);return;}finish(error);}
 const char*queue(const char*id,bool home,bool voice,JsonObject out,bool system=false,JsonObjectConst arguments=JsonObjectConst()){if(!entry(id).isNull()){
   JsonDocument runtime;host_.invoke("read",JsonObjectConst(),runtime.to<JsonObject>());
   if(voice&&!pending_.operation&&!strcmp(activeId_,id)&&(runtime["loaded"]|false)&&!(runtime["fault"]|false)&&runtime["file"]==activeEntry_){JsonDocument event;uint32_t operation=next_++;event["operation_id"]=operation;event["application_id"]=id;event["ok"]=true;out["operation_id"]=operation;out["application_id"]=id;out["state"]="accepted";core_.emit("/sys/apps","voice_requested",event.as<JsonObjectConst>());core_.emit("/sys/apps","launched",event.as<JsonObjectConst>(),system?0:core_.callerOwner());return nullptr;}
   Pending candidate;if(arguments.isNull())candidate.arguments.to<JsonObject>();else candidate.arguments.set(arguments);if(candidate.arguments.overflowed())return "application_memory_low";
   Pending*target=&pending_;
   if(loading_){JsonDocument runtime;host_.invoke("read",JsonObjectConst(),runtime.to<JsonObject>());if((runtime["stopped"]|false)||pending_.home||followup_.operation)return "application_transition_pending";target=&followup_;}
   else if(pending_.operation){if(!system)return "application_transition_pending";finish("application_superseded");}
   *target=std::move(candidate);copy(target->id,sizeof target->id,id);copy(target->path,sizeof target->path,entry(id)["manifest"]|"");target->operation=next_++;target->owner=system?0:core_.callerOwner();target->home=home;target->voice=voice;out["operation_id"]=target->operation;out["application_id"]=id;out["state"]="accepted";return nullptr;
  }return "application_not_installed";}
 const char*validateCatalog(JsonObjectConst c,bool persistent=false)const{
  if(!c["applications"].is<JsonArrayConst>())return "application_catalog";
  for(auto key:{"launcher","voice"})if(!c[key].is<const char*>())return "application_catalog";
  auto records=c["applications"].as<JsonArrayConst>();
  for(size_t i=0;i<records.size();++i){auto e=records[i];
   if(!identifier(e["id"]|"")||!e["manifest"].is<const char*>()||!e["title"].is<const char*>())return "application_catalog";
   for(size_t j=0;j<i;++j)if(records[j]["id"]==e["id"])return "duplicate_application_id";
   for(auto d:defaults_.as<JsonArrayConst>())if(d["id"]==e["id"])return "builtin_application_id";
   char normal[256];if(!normalizePath("/",e["manifest"],normal,sizeof normal)||strcmp(normal,e["manifest"]))return "application_path";
   if(persistent){const char*rel;auto provider=vfs_.resolve(normal,rel);if(!provider||!provider->persistent)return "persistent_application_provider_required";}
  }
  for(auto key:{"launcher","voice"}){const char*id=c[key]|"";if(!*id)continue;bool found=false;for(auto source:{records,defaults_.as<JsonArrayConst>()})for(auto item:source)if(item["id"]==id)found=true;if(!found)return "application_not_installed";}
  return measureJson(c)>catalogBytes_?"application_catalog_limit":nullptr;
 }
public:
 using Persist=bool(*)(const char*,size_t);
 static constexpr const char*Methods="read,list,inspect,install,remove,configure,launch,home,voice,exit";
 Applications(Core&c,Vfs&v,ScriptHost&h,const char*launcher,const char*voice="",size_t catalogBytes=8192):core_(c),vfs_(v),host_(h),factoryLauncher_(launcher),factoryVoice_(voice),catalogBytes_(catalogBytes){systemOwner_=core_.acquireOwner();activeArguments_.to<JsonObject>();catalog_["applications"].to<JsonArray>();catalog_["launcher"]=launcher;catalog_["voice"]=voice;}
 bool defaults(const char*json){JsonDocument next;if(deserializeJson(next,json)||!next.is<JsonArray>())return false;for(auto e:next.as<JsonArrayConst>())if(!identifier(e["id"]|"")||!e["manifest"].is<const char*>())return false;swap(defaults_,next);return true;}
 ~Applications(){if(pending_.preparation)core_.completions().release(pending_.preparation,systemOwner_);core_.releaseOwner(systemOwner_);}
 bool restore(const char*json){JsonDocument next;if(deserializeJson(next,json)||validateCatalog(next.as<JsonObjectConst>(),true)){restoreValid_=false;return false;}restoreValid_=true;swap(catalog_,next);persisted_=true;return true;}
 void describe(JsonObject out)const{out["installed_storage"]="package_files_on_discovered_volumes";out["execution_model"]="one_foreground_runtime_plus_isolated_workers";out["launcher_is_application"]=true;out["catalog_limit_bytes"]=catalogBytes_;out["switch_boundary"]="system_loop_after_current_callback";out["native_loading"]=false;for(auto m:{"launch","home","voice","exit"})asyncContract(out,m,"/sys/apps",{"launched","failed"})["operation_id"]="operation_id";out["read_methods"]="read,list,inspect";}
 bool transitionPending()const{return pending_.operation!=0;}
 bool hasLauncher()const{return !entry(catalog_["launcher"]|factoryLauncher_).isNull();}
 void requestHome(){JsonDocument out;queue(catalog_["launcher"]|factoryLauncher_,true,false,out.to<JsonObject>(),true);}
 void requestVoice(){JsonDocument out;queue(catalog_["voice"]|factoryVoice_,false,true,out.to<JsonObject>(),true);}
 void poll(){
  if(!pending_.operation){if(*activeId_){JsonDocument state;host_.invoke("read",JsonObjectConst(),state.to<JsonObject>());if((state["fault"]|false)&&state["file"]==activeEntry_){requestHome();}}return;}
  if((pending_.home||pending_.voice)&&!pending_.released){JsonDocument a,o;host_.invoke("stop",a.as<JsonObjectConst>(),o.to<JsonObject>());pending_.released=true;activeId_[0]=activeEntry_[0]=0;activeArguments_.to<JsonObject>();}
  const char*file=pending_.path;const char*relative;auto provider=vfs_.resolve(file,relative);
  if(!provider){failedActivation("application_storage_unavailable");return;}
  if(pending_.preparation){JsonDocument r;if(auto e=core_.completions().read(pending_.preparation,systemOwner_,r.to<JsonObject>())){failedActivation(e);return;}if(!(r["ready"]|false))return;JsonDocument schema;core_.describe(pending_.preparationSource,schema.to<JsonObject>());bool success=false;for(auto event:schema["async"][pending_.preparationMethod]["success_events"].as<JsonArrayConst>())if(event==r["event"])success=true;if(!success){failedActivation(r["data"]["error"]|"application_prepare_failed");return;}core_.completions().release(pending_.preparation,systemOwner_);pending_.preparation=0;pending_.prepared=true;}
  if(!provider->available()&&!pending_.prepared){JsonDocument description,result;provider->describe(description.to<JsonObject>());auto preparation=description["prepare"].as<JsonObjectConst>();if(preparation.isNull()){failedActivation("application_storage_unavailable");return;}JsonDocument schema;core_.describe(preparation["path"]|"",schema.to<JsonObject>());bool async=schema["async"].as<JsonObjectConst>().containsKey(preparation["method"]|"");const char*error=async?core_.request(preparation["path"],preparation["method"],preparation["args"],result.to<JsonObject>(),systemOwner_):core_.invoke(preparation["path"],preparation["method"],preparation["args"],result.to<JsonObject>(),systemOwner_);if(error){failedActivation(error);return;}pending_.prepared=true;if(async){pending_.preparation=result["completion_id"];copy(pending_.preparationSource,sizeof pending_.preparationSource,preparation["path"]);copy(pending_.preparationMethod,sizeof pending_.preparationMethod,preparation["method"]);return;}}
  if(auto e=manifest(file,pending_.manifest)){failedActivation(e);return;}
  if(pending_.manifest["id"]!=pending_.id){failedActivation("application_manifest_identity_changed");return;}
  JsonDocument args,out;args["file"]=pending_.manifest["entry"];for(auto key:{"memory_bytes","instruction_budget","system_call_budget"})if(pending_.manifest.containsKey(key))args[key]=pending_.manifest[key];
  loading_=true;auto error=host_.invoke("load",args.as<JsonObjectConst>(),out.to<JsonObject>());loading_=false;
  if(!error||!strcmp(error,"script_start_failed")){swap(activeArguments_,pending_.arguments);copy(activeId_,sizeof activeId_,pending_.id);copy(activeEntry_,sizeof activeEntry_,args["file"]);}
  if(error)failedActivation(error);else finish();
 }
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out,Persist save=nullptr){
  if(!strcmp(m,"launch")||!strcmp(m,"home")||!strcmp(m,"voice")||!strcmp(m,"exit")){
   for(auto f:a)if(strcmp(m,"launch")||!Core::containsMethod("id,arguments",f.key().c_str()))return "unknown_application_argument";
   if(a.containsKey("arguments")&&(!a["arguments"].is<JsonObjectConst>()||measureJson(a["arguments"])>1024))return "application_arguments";
   return queue(!strcmp(m,"launch")?(a["id"]|""):!strcmp(m,"voice")?(catalog_["voice"]|factoryVoice_):(catalog_["launcher"]|factoryLauncher_),strcmp(m,"launch")&&strcmp(m,"voice"),!strcmp(m,"voice"),out,false,a["arguments"].as<JsonObjectConst>());
  }
  if(!strcmp(m,"read")){if(a.size())return "unknown_application_argument";JsonDocument state;host_.invoke("read",JsonObjectConst(),state.to<JsonObject>());bool starting=loading_&&!(state["stopped"]|false);out["arguments"]=starting?pending_.arguments:activeArguments_;out["current"]=starting?pending_.id:state["file"]==activeEntry_?activeId_:"";out["runtime"]=state["loaded"];out["pending"]=pending_.id;out["pending_operation_id"]=pending_.operation;out["launcher"]=catalog_["launcher"];out["voice"]=catalog_["voice"];out["revision"]=revision_;out["persisted"]=persisted_;out["restore_valid"]=restoreValid_;out["last_error"]=lastError_;return nullptr;}
  if(!strcmp(m,"list")){for(auto f:a)if(!Core::containsMethod("cursor,limit",f.key().c_str())||!f.value().is<unsigned>())return "application_cursor_type";unsigned cursor=a["cursor"]|0u,limit=a["limit"]|4u;if(!limit||limit>4)return "application_page_limit";auto items=out["applications"].to<JsonArray>();unsigned seen=0;for(auto source:{defaults_.as<JsonArrayConst>(),catalog_["applications"].as<JsonArrayConst>()})for(auto e:source){if(seen++>=cursor&&items.size()<limit)items.add(e);}out["next_cursor"]=cursor+items.size();out["more"]=cursor+items.size()<seen;return nullptr;}
  if(!strcmp(m,"inspect")){for(auto f:a)if(strcmp(f.key().c_str(),"id"))return "unknown_application_argument";auto selected=entry(a["id"]|"");if(selected.isNull())return "application_not_installed";JsonDocument data;if(auto e=manifest(selected["manifest"],data,false))return e;out["application"]=data;return nullptr;}
  const char*fields=!strcmp(m,"install")?"manifest,persist,expected_revision":!strcmp(m,"remove")?"id,persist,expected_revision":!strcmp(m,"configure")?"launcher,voice,persist,expected_revision":"";
  for(auto f:a)if(!Core::containsMethod(fields,f.key().c_str()))return "unknown_application_argument";
  if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
  if(a.containsKey("persist")&&!a["persist"].is<bool>())return "application_persist_type";
  JsonDocument next;next.set(catalog_);
  if(!strcmp(m,"install")){const char*file=a["manifest"]|"";char normalized[256];if(!normalizePath("/",file,normalized,sizeof normalized)||strcmp(normalized,file))return "application_path";const char*rel;auto provider=vfs_.resolve(file,rel);if((a["persist"]|false)&&(!provider||!provider->persistent))return "persistent_application_provider_required";JsonDocument data;if(auto e=manifest(file,data))return e;
   for(auto item:defaults_.as<JsonArrayConst>())if(item["id"]==data["id"])return "builtin_application_id";
   auto apps=next["applications"].as<JsonArray>();JsonObject target;for(auto e:apps)if(e["id"]==data["id"])target=e.as<JsonObject>();if(target.isNull())target=apps.add<JsonObject>();target["id"]=data["id"];target["title"]=data["title"];target["manifest"]=file;if(data.containsKey("source"))target["source"]=data["source"];out["application_id"]=data["id"];
  }else if(!strcmp(m,"remove")){const char*id=a["id"]|"";JsonDocument state;host_.invoke("read",JsonObjectConst(),state.to<JsonObject>());bool active=(state["loaded"]|false)&&state["file"]==activeEntry_&&!strcmp(activeId_,id);if(catalog_["launcher"]==id||catalog_["voice"]==id||active||!strcmp(pending_.id,id))return "application_in_use";auto apps=next["applications"].as<JsonArray>();bool found=false;for(size_t i=0;i<apps.size();++i)if(apps[i]["id"]==id){apps.remove(i);found=true;break;}if(!found)return "application_not_installed";
  }else if(!strcmp(m,"configure")){if(core_.callerOwner())return "system_configuration_required";for(auto key:{"launcher","voice"})if(a.containsKey(key)){if(!a[key].is<const char*>()||entry(a[key]).isNull())return "application_not_installed";next[key]=a[key];}}
  else return "unsupported_method";
  if(auto e=validateCatalog(next.as<JsonObjectConst>(),a["persist"]|false))return e;if(next.overflowed())return "application_memory_low";
  bool persist=a["persist"]|false;if(persist){size_t n=measureJson(next);auto text=(char*)core_.resources().allocate(n+1);if(!text)return "application_memory_low";serializeJson(next,text,n+1);bool ok=save&&save(text,n);core_.resources().freeAllocation(text);if(!ok)return "persistence_failed";}
  swap(catalog_,next);++revision_;persisted_=persist;out["revision"]=revision_;JsonDocument event;event["revision"]=revision_;core_.emit("/sys/apps","catalog_changed",event.as<JsonObjectConst>());return nullptr;
 }
};
}
