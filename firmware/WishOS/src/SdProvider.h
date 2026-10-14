#pragma once
#include "Vfs.h"
#include "SdClockPlan.h"
#include <SdFat.h>
#include <Names.h>
#include <common/upcase.h>
#include <atomic>
namespace notesos {
class SdProvider:public FileProvider {
  FsVolume &sd_;bool &mounted_;bool(*take_)();void(*give_)();const char*(*ensure_)();
  FsFile readCache_,directoryCache_;char readPath_[256]{},directoryPath_[256]{};
  uint64_t readAt_=0;uint32_t directoryIndex_=0,cacheRevision_=0;
  void clearCaches(){readCache_.close();directoryCache_.close();readPath_[0]=directoryPath_[0]=0;directoryIndex_=0;readAt_=0;cacheRevision_=revision.load();}
  void checkCaches(){if(cacheRevision_!=revision.load())clearCaches();}
  struct Guard{SdProvider &p;bool locked;Guard(SdProvider&x):p(x),locked(x.take_()){}~Guard(){if(locked)p.give_();}};
public:
  std::atomic<uint32_t> revision{1};uint64_t freeBytes=0;std::atomic<bool> freeKnown{false};char reserved[192]{};
  uint64_t physicalBytes=0,volumeBytes=0;bool capacityKnown=false;
  void captureMount(uint64_t physical,uint64_t volume){clearCaches();physicalBytes=physical;volumeBytes=volume;capacityKnown=true;freeKnown=false;++revision;}
  void invalidate(){clearCaches();capacityKnown=false;freeKnown=false;physicalBytes=volumeBytes=0;++revision;}
  SdProvider(FsVolume&s,bool&m,bool(*t)(),void(*g)(),const char*(*e)()):FileProvider("user","/storage/user","sd",true,true,true),sd_(s),mounted_(m),take_(t),give_(g),ensure_(e){}
  void changed(){clearCaches();++revision;freeKnown=false;} // Caller holds the bus lease.
  const char*operations()const override{return "stat,read,list,write,create,resize,flush,remove,mkdir,rename";}
  uint64_t changeRevision()const override{return revision.load();}
  bool caseInsensitive()const override{return true;}
  bool sameName(const char*a,const char*b)const override{const char*ae=a+strlen(a),*be=b+strlen(b);while(a<ae&&b<be){uint32_t x,y;if(!esp32_nfs::name_detail::next(a,ae,x)||!esp32_nfs::name_detail::next(b,be,y))return false;if(x<=65535)x=toUpcase(uint16_t(x));if(y<=65535)y=toUpcase(uint16_t(y));if(x!=y)return false;}return a==ae&&b==be;}
  bool durableWrites()const override{return true;}
  uint64_t maxFileSize()const override{return sd_.fatType()==64?uint64_t(INT64_MAX):uint64_t(UINT32_MAX);}
  bool busyPath(const char*p)const override{if(!take_())return true;bool busy=protectedPath(p);give_();return busy;}
  const char*canonicalPath(const char*input,char*out,size_t cap,FileInfo&info)override{
    Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;
    if(esp32_nfs::name_detail::path(input,true)!=esp32_nfs::Ok)return "invalid_path";
    char path[256],canonical[256]="/";if(strlen(input)>=sizeof path)return "invalid_path";strcpy(path,input);
    auto root=sd_.open("/",O_RDONLY);if(!root)return "not_found";info={0,true};root.close();
    char*save=nullptr;for(char*part=strtok_r(path,"/",&save);part;part=strtok_r(nullptr,"/",&save)){
      char requested[256];if(snprintf(requested,sizeof requested,"%s%s%s",canonical,strlen(canonical)>1?"/":"",part)>=int(sizeof requested))return "invalid_path";
      auto f=sd_.open(requested,O_RDONLY);if(!f)return "not_found";char name[256];if(!f.getName(name,sizeof name)||esp32_nfs::name_detail::component(name,true)!=esp32_nfs::Ok)return "invalid_path";
      size_t at=strlen(canonical),n=strlen(name);bool sep=at>1;if(at+sep+n>=sizeof canonical)return "invalid_path";if(sep)canonical[at++]='/';memcpy(canonical+at,name,n+1);info={f.fileSize(),f.isDir()};
    }
    if(strlen(canonical)>=cap)return "invalid_path";memmove(out,canonical,strlen(canonical)+1);return nullptr;
  }
  const char*flush(const char*p)override{Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(protectedPath(p))return "file_in_use";clearCaches();auto f=sd_.open(p,O_RDONLY);return f&&f.sync()?nullptr:"sync_failed";}
  const char*createFile(const char*p)override{
    Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;
    if(!writable)return "read_only";if(protectedPath(p))return "file_in_use";
    clearCaches();++revision;freeKnown=false;auto f=sd_.open(p,O_CREAT|O_EXCL|O_RDWR);
    return f&&f.sync()?nullptr:"create_failed";
  }
  const char*resize(const char*p,uint64_t size)override{
    Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;
    if(!writable)return "read_only";if(protectedPath(p))return "file_in_use";
    clearCaches();auto f=sd_.open(p,O_RDWR);if(!f)return "not_found";if(f.isDir())return "is_directory";
    if(size==f.fileSize())return nullptr;if(size>f.fileSize())return "unsupported_operation";
    ++revision;freeKnown=false;return f.truncate(size)&&f.sync()?nullptr:"truncate_failed";
  }
  // Explicit volume metadata scan, never used by presence/link probing or status reads.
  const char*refreshSpace()override{Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;
    auto n=sd_.freeClusterCount();if(n<0)return "space_scan_failed";
    freeBytes=uint64_t(n)*sd_.bytesPerCluster();freeKnown=true;return nullptr;
  }
  bool available()const override{return mounted_;}
  bool protectedPath(const char*p)const{if(!reserved[0])return false;char q[256];if(strlen(p)>=sizeof q)return true;
    if(sameName(reserved,p))return true;strcpy(q,p);if(auto suffix=strrchr(q,'.')){*suffix=0;if(sameName(reserved,q))return true;}
    strcpy(q,reserved);for(char*slash=strrchr(q,'/');slash;slash=strrchr(q,'/')){*slash=0;if(sameName(q,p))return true;}return false;
  }
  void describe(JsonObject o)const override{
    o["prepare"]["path"]="/dev/sd";o["prepare"]["method"]="mount";o["prepare"]["args"].to<JsonObject>();
    o["filename_encoding"]="utf8";o["mounted"]=mounted_;o["filesystem"]=mounted_?(sd_.fatType()==64?"exfat":sd_.fatType()==32?"fat32":"fat16"):"unknown";
    o["revision"]=revision.load();o["partition_resize_supported"]=false;o["free_is_cached"]=true;
    if(mounted_&&capacityKnown){sizeText(o,"physical_capacity_bytes",physicalBytes);sizeText(o,"volume_capacity_bytes",volumeBytes);}else{o["physical_capacity_bytes"]=nullptr;o["volume_capacity_bytes"]=nullptr;}
    if(freeKnown)sizeText(o,"free_bytes",freeBytes);else o["free_bytes"]=nullptr;
  }
  const char*ready(){auto e=ensure_();return e?e:mounted_?nullptr:"volume_not_mounted";}
  const char*stat(const char*p,FileInfo&o)override{Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(protectedPath(p))return "file_in_use";auto f=sd_.open(p,O_RDONLY);if(!f)return "not_found";o={f.fileSize(),f.isDir()};return nullptr;}
  const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{got=0;Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(protectedPath(p))return "file_in_use";if(strlen(p)>=sizeof readPath_)return "invalid_path";checkCaches();
    if(strcmp(p,readPath_)){readCache_.close();readPath_[0]=0;readCache_=sd_.open(p,O_RDONLY);if(!readCache_||readCache_.isDir())return "not_file";strcpy(readPath_,p);readAt_=0;}
    if(at>=readCache_.fileSize())return nullptr;if(at!=readAt_&&!readCache_.seekSet(at)){clearCaches();return "seek_failed";}int r=readCache_.read(b,n);if(r<0||(!r&&n)){clearCaches();return "read_failed";}got=size_t(r);readAt_=at+got;return nullptr;}

  const char*write(const char*p,uint64_t at,const uint8_t*b,size_t n,bool trunc,size_t&wrote)override{wrote=0;if(trunc&&at)return "truncate_offset";Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(!writable)return "read_only";if(protectedPath(p))return "file_in_use";clearCaches();++revision;freeKnown=false;auto f=sd_.open(p,O_RDWR|O_CREAT|(trunc?O_TRUNC:0));if(!f||!f.seekSet(at))return "open_or_seek_failed";wrote=f.write(b,n);if(wrote!=n||!f.sync())return "write_failed";return nullptr;}
  const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{
    Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(strlen(p)>=sizeof directoryPath_)return "invalid_path";checkCaches();
    if(strcmp(p,directoryPath_)||cursor<directoryIndex_){directoryCache_.close();directoryPath_[0]=0;directoryCache_=sd_.open(p,O_RDONLY);if(!directoryCache_||!directoryCache_.isDir())return "not_directory";strcpy(directoryPath_,p);directoryIndex_=0;}
    FsFile f;auto arr=o["entries"].to<JsonArray>();bool more=false;
    while(arr.size()<limit){if(!f.openNext(&directoryCache_,O_RDONLY))break;
      if(directoryIndex_++<cursor){f.close();continue;}char name[256];if(!f.getName(name,sizeof name)){clearCaches();return "directory_name_failed";}auto x=arr.add<JsonObject>();if(x.isNull())return "memory_low";x["name"]=name;x["directory"]=f.isDir();sizeText(x,"size",f.fileSize());f.close();}
    if(directoryCache_.getError()){clearCaches();return "directory_read_failed";}
    more=limit&&arr.size()==limit;o["next_cursor"]=cursor+arr.size();o["more"]=more;o["revision"]=revision.load();return nullptr;
  }
  const char*remove(const char*p)override{Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(!writable)return "read_only";if(protectedPath(p))return "file_in_use";clearCaches();auto f=sd_.open(p,O_RDONLY);if(!f)return "not_found";bool dir=f.isDir();f.close();bool ok=dir?sd_.rmdir(p):sd_.remove(p);if(ok){++revision;freeKnown=false;}return ok?nullptr:"remove_failed";}
  const char*mkdir(const char*p)override{Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(!writable)return "read_only";if(protectedPath(p))return "file_in_use";clearCaches();if(!sd_.mkdir(p,true))return "mkdir_failed";++revision;freeKnown=false;return nullptr;}
  const char*rename(const char*a,const char*b)override{Guard g(*this);if(!g.locked)return "spi_busy";if(auto e=ready())return e;if(!writable)return "read_only";if(protectedPath(a)||protectedPath(b))return "file_in_use";clearCaches();if(!sd_.rename(a,b))return "rename_failed";++revision;return nullptr;}
};
}
