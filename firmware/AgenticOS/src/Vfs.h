#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
namespace notesos {
struct FileInfo { uint64_t size=0; bool directory=false; };
inline bool parseOffset(JsonVariantConst v,uint64_t &out){
  if(v.isNull()){out=0;return true;}
  if(v.is<const char*>()){const char*p=v.as<const char*>();if(!*p)return false;out=0;
    for(;*p;++p){if(*p<'0'||*p>'9'||out>(UINT64_MAX-(*p-'0'))/10)return false;out=out*10+(*p-'0');}return true;}
  if(v.is<uint32_t>()){out=v.as<uint32_t>();return true;}return false;
}
inline void sizeText(JsonObject out,const char*key,uint64_t value){char s[24];snprintf(s,sizeof(s),"%llu",(unsigned long long)value);out[key]=s;}
inline bool pathWithin(const char*root,const char*path){size_t n=strlen(root);return !strcmp(root,"/")||(!strncmp(root,path,n)&&(!path[n]||path[n]=='/'));}
inline bool normalizePath(const char *base,const char *path,char*out,size_t cap,const char*boundary="/"){
  if(!base||!path||!out||cap<2||strchr(path,'\\')||strchr(path,'?')||strchr(path,'#'))return false;
  char combined[512];int n=path[0]=='/'?snprintf(combined,sizeof(combined),"%s",path):snprintf(combined,sizeof(combined),"%s/%s",base,path);
  if(n<0||n>=int(sizeof(combined)))return false;size_t at=1;out[0]='/';out[1]=0;
  char *save=nullptr;
  for(char*t=strtok_r(combined,"/",&save);t;t=strtok_r(nullptr,"/",&save)){
    if(!strcmp(t,"."))continue;
    if(!strcmp(t,"..")){if(at<=1)return false;while(at>1&&out[at-1]!='/')--at;if(at>1)--at;out[at]=0;continue;}
    for(const char*p=t;*p;++p)if((unsigned char)*p<32)return false;
    size_t z=strlen(t);if(at+(at>1?1:0)+z+1>cap)return false;if(at>1)out[at++]='/';memcpy(out+at,t,z+1);at+=z;
  }
  size_t b=strlen(boundary);return !strcmp(boundary,"/")||(!strncmp(out,boundary,b)&&(out[b]==0||out[b]=='/'));
}
class FileProvider {
public:
  const char *id,*mount,*medium;bool persistent,removable,writable;
  FileProvider(const char*i,const char*m,const char*t,bool p,bool r,bool w):id(i),mount(m),medium(t),persistent(p),removable(r),writable(w){}
  virtual ~FileProvider()=default;
  virtual uint64_t changeRevision()const{return 0;}
  virtual bool caseInsensitive()const{return false;}
  virtual const char*canonicalPath(const char*,char*,size_t,FileInfo&){return "unsupported_operation";}
  virtual bool sameName(const char*a,const char*b)const{return caseInsensitive()?!strcasecmp(a,b):!strcmp(a,b);}
  virtual bool durableWrites()const{return false;}
  virtual uint64_t maxFileSize()const{return UINT32_MAX;}
  virtual const char*refreshSpace(){return "unsupported_operation";}
  virtual bool busyPath(const char*)const{return false;}
  virtual const char*createFile(const char*){return "unsupported_operation";}
  virtual const char*resize(const char*,uint64_t){return "unsupported_operation";}
  virtual const char*flush(const char*){return "unsupported_operation";}
  virtual bool available()const=0;
  virtual void describe(JsonObject out)const=0;
  virtual const char* stat(const char*,FileInfo&)=0;
  virtual const char* read(const char*,uint64_t,uint8_t*,size_t,size_t&)=0;
  virtual const char* write(const char*,uint64_t,const uint8_t*,size_t,bool,size_t&)=0;
  virtual const char* list(const char*,uint32_t,uint32_t,JsonObject)=0;
  virtual const char* remove(const char*)=0;
  virtual const char* mkdir(const char*)=0;
  virtual const char* rename(const char*,const char*)=0;
};
class Vfs {
  FileProvider* providers_[8]{};size_t count_=0;
  struct Lease{char path[256]{};uint32_t owner=0;};Lease leases_[16];
public:
  bool lockRead(const char*path,uint32_t owner){if(!owner||strlen(path)>=256)return false;for(auto&l:leases_)if(l.owner==owner&&!strcmp(l.path,path))return true;for(auto&l:leases_)if(!l.owner){strcpy(l.path,path);l.owner=owner;return true;}return false;}
  void release(uint32_t owner){for(auto&l:leases_)if(l.owner==owner)l={};}
  bool hasLeasesUnder(const char*path)const{for(auto&l:leases_)if(l.owner&&(pathWithin(path,l.path)||pathWithin(l.path,path)))return true;return false;}
  bool canMutate(const char*path,bool folded=false)const{
    if(!folded)return !hasLeasesUnder(path);
    const char*relative=nullptr;auto provider=resolve(path,relative);
    auto within=[&](const char*root,const char*child){
      if(!strcmp(root,"/"))return true;
      while(*root&&*child){while(*root=='/')++root;while(*child=='/')++child;char a[256],b[256];size_t n=0,m=0;
        while(root[n]&&root[n]!='/'){if(n>=255)return false;a[n]=root[n];++n;}a[n]=0;
        while(child[m]&&child[m]!='/'){if(m>=255)return false;b[m]=child[m];++m;}b[m]=0;
        if(provider?!provider->sameName(a,b):strcasecmp(a,b))return false;root+=n;child+=m;
      }return !*root;
    };
    for(auto&l:leases_)if(l.owner&&(within(path,l.path)||within(l.path,path)))return false;return true;
  }
  struct Pin{FileProvider*provider=nullptr;uint32_t owner=0;};Pin pins[8];
  bool pin(FileProvider&p,uint32_t owner){if(!owner)return false;for(auto&x:pins)if(!x.owner){x={&p,owner};return true;}return false;}
  void unpin(uint32_t owner){for(auto&x:pins)if(x.owner==owner)x={};}
  bool pinned(const FileProvider&p)const{for(auto&x:pins)if(x.provider==&p)return true;return false;}
  bool pinnedPath(const char*path)const{for(auto&x:pins)if(x.provider&&(pathWithin(path,x.provider->mount)||pathWithin(x.provider->mount,path)))return true;return false;}
  bool add(FileProvider &p){if(count_==8)return false;for(size_t i=0;i<count_;++i)if(!strcmp(providers_[i]->mount,p.mount)||!strcmp(providers_[i]->id,p.id))return false;providers_[count_++]=&p;return true;}
  FileProvider* resolve(const char* path,const char*&relative)const{
    FileProvider*best=nullptr;size_t longest=0;
    for(size_t i=0;i<count_;++i){auto p=providers_[i];size_t n=strlen(p->mount);if(n>=longest&&!strncmp(path,p->mount,n)&&(path[n]=='/'||!path[n])){best=p;longest=n;}}
    relative=best?(path[longest]?path+longest:"/"):nullptr;return best;
  }
  FileProvider* get(const char*id)const{for(size_t i=0;i<count_;++i)if(!strcmp(providers_[i]->id,id))return providers_[i];return nullptr;}
  void describe(JsonArray out)const{for(size_t i=0;i<count_;++i){auto p=providers_[i];auto x=out.add<JsonObject>();x["id"]=p->id;x["mount"]=p->mount;x["medium"]=p->medium;x["persistent"]=p->persistent;x["removable"]=p->removable;x["writable"]=p->writable;x["durable_writes"]=p->durableWrites();x["case_insensitive"]=p->caseInsensitive();x["available"]=p->available();p->describe(x);}}
};
class MemoryFiles:public FileProvider {
public:
  using Allocate=void*(*)(size_t);using Release=void(*)(void*);
private:
  struct Entry {char path[192]{};uint8_t*bytes=nullptr;size_t size=0,capacity=0;bool directory=false;};
  uint64_t revision_=1;
  Entry files_[24];size_t budget_=32768,used_=0;Allocate allocate_;Release release_;bool present_;
  Entry* find(const char*p){for(auto&e:files_)if(!strcmp(e.path,p))return &e;return nullptr;}
  bool parentExists(const char*p){char b[192];if(strlen(p)>=sizeof(b))return false;strcpy(b,p);char*s=strrchr(b,'/');if(!s)return false;if(s==b)return true;*s=0;auto e=find(b);return e&&e->directory;}
public:
  MemoryFiles(const char*id,const char*mount,const char*medium,bool present=true,Allocate a=malloc,Release r=free):FileProvider(id,mount,medium,false,false,true),allocate_(a),release_(r),present_(present){}
  ~MemoryFiles(){for(auto&e:files_)if(e.bytes)release_(e.bytes);}
  uint64_t changeRevision()const override{return revision_;}
  uint64_t maxFileSize()const override{return budget_;}
  const char*flush(const char*p)override{FileInfo i;return stat(p,i);}
  const char*createFile(const char*p)override{if(find(p))return "exists";size_t wrote=0;uint8_t b=0;return write(p,0,&b,0,false,wrote);}
  const char*resize(const char*p,uint64_t size)override{
    if(!present_)return "provider_unavailable";auto e=find(p);if(!e)return "not_found";if(e->directory)return "is_directory";
    if(size>budget_-used_+e->capacity)return "quota_exceeded";
    auto next=size?(uint8_t*)allocate_(size):nullptr;if(size&&!next)return "memory_low";
    if(size){memset(next,0,size);if(e->size)memcpy(next,e->bytes,size<e->size?size:e->size);}
    if(e->bytes)release_(e->bytes);used_=used_-e->capacity+size;e->size=e->capacity=size;e->bytes=next;++revision_;return nullptr;
  }
  bool available()const override{return present_;}
  void setPresent(bool p){present_=p;}
  bool budget(size_t n){if(n<used_)return false;budget_=n;return true;}
  void describe(JsonObject o)const override{sizeText(o,"quota_bytes",budget_);sizeText(o,"allocated_bytes",used_);sizeText(o,"quota_free_bytes",budget_-used_);o["filesystem"]="memory";o["physical_capacity_bytes"]=nullptr;o["physical_partition_mutable"]=false;}
  const char*stat(const char*p,FileInfo&o)override{if(!present_)return "provider_unavailable";if(!strcmp(p,"/")){o={0,true};return nullptr;}auto e=find(p);if(!e)return "not_found";o={e->size,e->directory};return nullptr;}
  const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{got=0;if(!present_)return "provider_unavailable";auto e=find(p);if(!e)return "not_found";if(e->directory)return "is_directory";if(at>=e->size)return nullptr;got=n<e->size-at?n:e->size-at;memcpy(b,e->bytes+at,got);return nullptr;}
  const char*write(const char*p,uint64_t at,const uint8_t*b,size_t n,bool trunc,size_t&wrote)override{
    wrote=0;if(!present_)return "provider_unavailable";if(strlen(p)>=192||!parentExists(p)||at>budget_||n>budget_-at|| (trunc&&at))return "invalid_path_or_quota";
    auto e=find(p);if(e&&e->directory)return "is_directory";if(!e)for(auto&x:files_)if(!x.path[0]){e=&x;break;}if(!e)return "entry_capacity";
    size_t target=size_t(at)+n;size_t old=trunc?0:e->size;size_t cap=target>old?target:old;
    if(cap>budget_-used_+e->capacity)return "quota_exceeded";
    uint8_t*next=cap?(uint8_t*)allocate_(cap):nullptr;if(cap&&!next)return "memory_low";
    if(cap)memset(next,0,cap);if(old)memcpy(next,e->bytes,old);if(n)memcpy(next+at,b,n);
    if(e->bytes)release_(e->bytes);used_=used_-e->capacity+cap;e->bytes=next;e->capacity=cap;e->size=cap;++revision_;strcpy(e->path,p);wrote=n;return nullptr;
  }
  const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{
    FileInfo info;if(auto e=stat(p,info))return e;if(!info.directory)return "not_directory";auto arr=o["entries"].to<JsonArray>();uint32_t seen=0,next=cursor;bool more=false;
    size_t plen=strlen(p);for(auto&e:files_){if(!e.path[0])continue;const char*name=nullptr;if(!strcmp(p,"/"))name=e.path+1;else if(!strncmp(e.path,p,plen)&&e.path[plen]=='/')name=e.path+plen+1;if(!name||!*name||strchr(name,'/'))continue;
      if(seen++<cursor)continue;if(arr.size()==limit){more=true;break;}auto x=arr.add<JsonObject>();x["name"]=name;x["directory"]=e.directory;sizeText(x,"size",e.size);++next;}
    o["next_cursor"]=next;o["more"]=more;return nullptr;
  }
  const char*remove(const char*p)override{if(!present_)return "provider_unavailable";auto e=find(p);if(!e)return "not_found";if(e->directory)for(auto&x:files_)if(!strncmp(x.path,p,strlen(p))&&x.path[strlen(p)]=='/')return "directory_not_empty";used_-=e->capacity;if(e->bytes)release_(e->bytes);*e={};++revision_;return nullptr;}
  const char*mkdir(const char*p)override{if(!present_)return "provider_unavailable";if(!strcmp(p,"/"))return nullptr;if(find(p))return "exists";if(strlen(p)>=192||!parentExists(p))return "invalid_path";for(auto&e:files_)if(!e.path[0]){strcpy(e.path,p);e.directory=true;++revision_;return nullptr;}return "entry_capacity";}
  const char*rename(const char*a,const char*b)override{if(!present_)return "provider_unavailable";auto e=find(a);if(!e)return "not_found";if(e->directory)return "directory_rename_not_supported";if(find(b))return "exists";if(strlen(b)>=192||!parentExists(b))return "invalid_path";strcpy(e->path,b);++revision_;return nullptr;}
};
}
