#pragma once
#include "Vfs.h"
#include "BoardPackage.h"
namespace notesos {
// Immutable package assets share the same VFS contract as user files.
class SystemFiles:public FileProvider {
public:
  SystemFiles():FileProvider("system","/system","internal_flash",true,false,false){}
  bool durableWrites()const override{return true;}
  const char*flush(const char*)override{return nullptr;}
  bool available()const override{return true;}
  void describe(JsonObject o)const override{size_t bytes=0;for(auto i:boardpack::ASSETS)bytes+=strlen(i.data);sizeText(o,"volume_capacity_bytes",bytes);o["filesystem"]="compiled_readonly";o["free_bytes"]="0";o["package"]=boardpack::ID;}
  const char*stat(const char*p,FileInfo&o)override{if(!strcmp(p,"/")){o={0,true};return nullptr;}for(auto i:boardpack::ASSETS){if(!strcmp(p,i.path)){o={strlen(i.data),false};return nullptr;}if(pathWithin(p,i.path)){o={0,true};return nullptr;}}return "not_found";}
  const char*read(const char*p,uint64_t at,uint8_t*out,size_t length,size_t&got)override{got=0;for(auto i:boardpack::ASSETS)if(!strcmp(p,i.path)){size_t total=strlen(i.data);if(at>=total)return nullptr;got=length<total-at?length:total-at;memcpy(out,i.data+at,got);return nullptr;}return "not_found";}
  const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{
    FileInfo info;if(auto e=stat(p,info))return e;if(!info.directory)return "not_directory";
    auto arr=o["entries"].to<JsonArray>();size_t offset=!strcmp(p,"/")?1:strlen(p)+1;uint32_t index=0;
    for(size_t k=0;k<boardpack::ASSET_COUNT;++k){auto path=boardpack::ASSETS[k].path;if(!pathWithin(p,path)||strlen(path)<offset)continue;
      const char*name=path+offset;const char*slash=strchr(name,'/');size_t n=slash?size_t(slash-name):strlen(name);bool duplicate=false;
      for(size_t j=0;j<k;++j){auto prev=boardpack::ASSETS[j].path;if(pathWithin(p,prev)&&strlen(prev)>=offset&&!strncmp(prev+offset,name,n)&&(!prev[offset+n]||prev[offset+n]=='/'))duplicate=true;}
      if(duplicate)continue;if(index>=cursor&&arr.size()<limit){auto e=arr.add<JsonObject>();char part[256];memcpy(part,name,n);part[n]=0;e["name"]=part;e["directory"]=slash!=nullptr;}++index;
    }o["next_cursor"]=cursor+arr.size();o["more"]=cursor+arr.size()<index;return nullptr;
  }
  const char*write(const char*,uint64_t,const uint8_t*,size_t,bool,size_t&)override{return "read_only";}
  const char*remove(const char*)override{return "read_only";}const char*mkdir(const char*)override{return "read_only";}const char*rename(const char*,const char*)override{return "read_only";}
};
}
