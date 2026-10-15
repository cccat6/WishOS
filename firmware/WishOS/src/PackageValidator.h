#pragma once
#include "LuaSyntax.h"
#include "Sha256.h"
#include "BudgetJson.h"
namespace notesos {
// Incremental native validation owns only a candidate revision. It never runs an
// app or changes the catalog. One poll hashes one chunk or validates one source.
class PackageValidator {
 Core&core_;Vfs&vfs_;BudgetJson json_;FileProvider*provider_=nullptr;Vfs::Access*lease_=nullptr;
 uint32_t pin_=0;uint64_t inventoryAt_=0,inventorySize_=0,at_=0,size_=0;size_t lineSize_=0;unsigned count_=0;
 char root_[256]{},package_[256]{},manifest_[256]{},inventory_[256]{},file_[256]{},program_[256]{},last_[256]{},expected_[65]{},inventoryExpected_[65]{},error_[80]{};
 uint8_t*line_=nullptr;Sha256 inventoryHash_,fileHash_;bool done_=false,lineReady_=false,foundEntry_=false,foundProgram_=false;
 static constexpr size_t LineLimit=1024,JsonLimit=16384;
 static bool digest(const char*s){return s&&strlen(s)==64&&strspn(s,"0123456789abcdef")==64;}
 static bool transient(const char*e){return e&&Core::containsMethod("spi_busy,resource_busy",e);}
 const char*resolve(const char*name,char*out){if(!name||!*name||*name=='/'||!normalizePath(root_,name,out,256,root_))return "package_path";const char*relative=out+strlen(root_)+1;return strcmp(relative,name)?"package_noncanonical_path":nullptr;}
 const char*canonical(const char*full){if(!provider_->caseInsensitive())return nullptr;const char*rel;vfs_.resolve(full,rel);char canonical[256];FileInfo info;if(auto e=provider_->canonicalPath(rel,canonical,sizeof canonical,info))return e;return strcmp(canonical,rel)?"package_noncanonical_path":nullptr;}
 const char*jsonFile(const char*full,JsonDocument&out,size_t maximum){const char*relative;vfs_.resolve(full,relative);FileInfo info;if(auto e=provider_->stat(relative,info))return e;if(info.directory||!info.size||info.size>maximum)return "package_json_limit";auto bytes=static_cast<uint8_t*>(core_.resources().allocate(info.size));if(!bytes)return "package_memory_budget";size_t got=0;auto e=provider_->read(relative,0,bytes,info.size,got);if(!e&&got!=info.size)e="package_short_read";if(!e&&deserializeJson(out,bytes,got,DeserializationOption::NestingLimit(16)))e="package_json";core_.resources().freeAllocation(bytes);return e;}
 const char*content(){const char*extension=strrchr(file_,'.');JsonDocument out(&json_);
  if(!strcmp(file_,program_)||(extension&&!strcmp(extension,".lua")))return LuaSyntax::file(core_,vfs_,file_,out.to<JsonObject>());
  if(!extension||strcmp(extension,".json"))return nullptr;
  JsonDocument doc(&json_);if(auto e=jsonFile(file_,doc,JsonLimit))return e;
  const char*method=doc["version"].is<const char*>()&&doc["assets"].is<JsonArrayConst>()?"validate":doc["format"]=="notes-app/1"?"validate_app":nullptr;
  if(method){doc.clear();auto device=core_.find("/sys/ui");if(!device||!core_.supports(*device,method))return "package_ui_validation_unavailable";JsonDocument a;a[!strcmp(method,"validate_app")?"file":"path"]=file_;return core_.invoke("/sys/ui",method,a.as<JsonObjectConst>(),out.to<JsonObject>());}
  return nullptr;
 }
 void fail(const char*error){copy(error_,sizeof error_,error);done_=true;}
 const char*line(){if(lineReady_)return nullptr;const char*relative;vfs_.resolve(inventory_,relative);uint8_t bytes[256];
  if(inventoryAt_<inventorySize_){size_t got=0;if(auto e=provider_->read(relative,inventoryAt_,bytes,std::min<uint64_t>(sizeof bytes,inventorySize_-inventoryAt_),got))return e;if(!got)return "package_short_read";
   for(size_t i=0;i<got;++i){inventoryHash_.update(bytes+i,1);++inventoryAt_;if(bytes[i]=='\n'){if(lineSize_){lineReady_=true;return nullptr;}continue;}if(lineSize_==LineLimit)return "package_inventory_line_limit";line_[lineSize_++]=bytes[i];}
  }if(inventoryAt_==inventorySize_&&lineSize_)lineReady_=true;return nullptr;
 }
public:
 PackageValidator(Core&c,Vfs&v):core_(c),vfs_(v),json_(c.resources()){}~PackageValidator(){release();}
 bool active()const{return lease_!=nullptr;}bool done()const{return done_;}const char*error()const{return error_;}const char*package()const{return package_;}const char*manifest()const{return manifest_;}const char*root()const{return root_;}const char*inventoryDigest()const{return inventoryExpected_;}
 void release(){delete lease_;lease_=nullptr;if(pin_)vfs_.unpin(pin_);pin_=0;provider_=nullptr;core_.resources().freeAllocation(line_);line_=nullptr;}
 void cancel(const char*error){fail(error);}
 const char*start(const char*manifest,const char*package,uint32_t pin,const char*program){if(active())return "application_validation_in_use";done_=lineReady_=foundEntry_=false,foundProgram_=false;error_[0]=file_[0]=last_[0]=0;inventoryAt_=inventorySize_=at_=size_=lineSize_=count_=0;inventoryHash_.reset();
  if(!normalizePath("/",package,package_,sizeof package_)||strcmp(package_,package)||!normalizePath("/",manifest,manifest_,sizeof manifest_)||strcmp(manifest_,manifest))return "package_path";
  copy(root_,sizeof root_,package_);auto slash=strrchr(root_,'/');if(!slash||slash==root_)return "package_root";*slash=0;if(!normalizePath("/",program,program_,sizeof program_)||!pathWithin(root_,program_))return "package_program_path";const char*relative;provider_=vfs_.resolve(package_,relative);if(!provider_||!provider_->available())return "application_storage_unavailable";
  lease_=new(std::nothrow)Vfs::Access(vfs_,false);if(!lease_)return "package_memory_budget";if(auto e=lease_->add(root_,provider_->caseInsensitive())){release();return e;}if(!vfs_.pin(*provider_,pin)){release();return "provider_lease_capacity";}pin_=pin;
  JsonDocument header(&json_);auto e=jsonFile(package_,header,LineLimit);if(!e)e=argumentFields(header.as<JsonObjectConst>(),"format,entry,inventory,inventory_sha256");
  char entry[256];if(!e&&(header["format"]!="wishos-package/1"||!header["entry"].is<const char*>()||!header["inventory"].is<const char*>()||!digest(header["inventory_sha256"]|"")))e="package_manifest";
  if(!e)e=resolve(header["entry"],entry);if(!e&&strcmp(entry,manifest_))e="package_entry_mismatch";if(!e)e=resolve(header["inventory"],inventory_);if(!e&&(!strcmp(inventory_,manifest_)||!strcmp(inventory_,package_)))e="package_inventory_path";
  if(!e){vfs_.resolve(inventory_,relative);FileInfo info;e=provider_->stat(relative,info);if(!e&&(info.directory||!info.size))e="package_inventory";inventorySize_=info.size;copy(inventoryExpected_,sizeof inventoryExpected_,header["inventory_sha256"]);}
  if(!e){line_=static_cast<uint8_t*>(core_.resources().allocate(LineLimit+1));if(!line_)e="package_memory_budget";}if(e)release();return e;
 }
 void poll(){if(!active()||done_)return;const char*e=nullptr;const char*relative;
  if(*file_){vfs_.resolve(file_,relative);if(at_<size_){uint8_t bytes[1024];size_t got=0;e=provider_->read(relative,at_,bytes,std::min<uint64_t>(sizeof bytes,size_-at_),got);if(!e){if(!got)e="package_short_read";else{fileHash_.update(bytes,got);at_+=got;}}}
   else{char actual[65];fileHash_.finish(actual);if(strcmp(actual,expected_))e="package_digest_mismatch";else e=content();if(!e){++count_;file_[0]=0;}}
  }else{e=line();if(!e&&lineReady_){JsonDocument row(&json_);if(deserializeJson(row,line_,lineSize_))e="package_inventory_json";else if((e=argumentFields(row.as<JsonObjectConst>(),"path,bytes,sha256"))){}else if(!row["path"].is<const char*>()||!row["bytes"].is<const char*>()||!parseOffset(row["bytes"],size_)||!digest(row["sha256"]|""))e="package_inventory_entry";
    char full[256];if(!e)e=resolve(row["path"],full);if(!e&&(*last_&&strcmp(last_,row["path"])>=0||!strcmp(full,package_)||!strcmp(full,inventory_)))e="package_inventory_order";
    if(!e)e=canonical(full);if(!e){FileInfo info;vfs_.resolve(full,relative);e=provider_->stat(relative,info);if(!e&&(info.directory||info.size!=size_))e="package_size_mismatch";}
    if(!e){if(!strcmp(full,manifest_))foundEntry_=true;if(!strcmp(full,program_))foundProgram_=true;copy(last_,sizeof last_,row["path"]);copy(file_,sizeof file_,full);copy(expected_,sizeof expected_,row["sha256"]);at_=0;fileHash_.reset();lineSize_=0;lineReady_=false;}
   }else if(!e&&inventoryAt_==inventorySize_){char actual[65];inventoryHash_.finish(actual);if(strcmp(actual,inventoryExpected_))e="package_inventory_digest_mismatch";else if(!count_||!foundEntry_||!foundProgram_)e="package_entry_missing";else{done_=true;}}}
  if(e&&!transient(e))fail(e);
 }
 void describe(JsonObject out)const{out["format"]="wishos-package/1";out["inventory"]="sorted_jsonl";out["inventory_line_limit_bytes"]=LineLimit;out["header_limit_bytes"]=LineLimit;out["json_limit_bytes"]=JsonLimit;out["lua_source_limit_bytes"]=LuaSyntax::SourceLimit;out["lua_parser"]=LUA_RELEASE;out["native_execution"]=false;out["hash_chunk_bytes"]=1024;out["file_count_limit"]=nullptr;}
 void read(JsonObject out)const{out["state"]=done_?(*error_?"failed":"validated"):active()?"validating":"idle";out["package"]=package_;out["manifest"]=manifest_;out["root"]=root_;out["inventory_sha256"]=inventoryExpected_;out["current_file"]=file_;out["validated_files"]=count_;out["error"]=error_;}
};
}
