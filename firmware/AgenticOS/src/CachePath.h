#pragma once
#include "Vfs.h"
#include <ctype.h>
namespace notesos {
inline const char*resolveCachePath(FileProvider&p,const char*key,JsonObject out){
  if(!key||!*key||strlen(key)>96)return "invalid_cache_key";
  for(auto s=key;*s;++s)if(!isalnum((unsigned char)*s)&&*s!='-'&&*s!='_'&&*s!='.')return "invalid_cache_key";
  if(!strcmp(key,".")||!strcmp(key,".."))return "invalid_cache_key";
  char full[256];int n=snprintf(full,sizeof(full),"%s/cache/%s",p.mount,key);
  if(n<0||size_t(n)>=sizeof(full))return "path_limit";
  out["file"]=full;out["provider"]=p.id;out["persistent"]=p.persistent;
  out["available"]=p.available();out["writable"]=p.writable;out["reserved"]=false;
  return nullptr;
}
inline const char*prepareCacheDirectory(FileProvider&p){
  if(!p.writable)return "read_only";
  FileInfo info;auto e=p.stat("/cache",info);
  if(!e)return info.directory?nullptr:"cache_path_not_directory";
  if(strcmp(e,"not_found"))return e;
  return p.mkdir("/cache");
}
}
