#pragma once
#include "Vfs.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <atomic>
#include <mutex>
namespace notesos {
// The native VFS supplies exclusive create, truncate and error-preserving I/O;
// Arduino File's reduced API cannot implement the same provider contract.
class PosixFileProvider:public FileProvider {
protected:
 bool&mounted_;const char*base_;const char*resource_;const char*filesystem_;uint32_t unit_;std::recursive_mutex*bus_;
private:
std::recursive_mutex localBus_;struct Reader{int fd=-1;char path[320]{};uint64_t used=0;};Reader readers_[2];uint64_t readOrder_=0;
 void closeReader(){for(auto&r:readers_){if(r.fd>=0)::close(r.fd);r={};}}
 std::atomic<uint64_t>revision_{1};mutable std::mutex spaceMutex_;uint64_t capacity_=0,free_=0;bool known_=false,dirty_=true;std::atomic<bool>background_{false};void*spaceContext_=nullptr;const char*(*requestSpace_)(void*)=nullptr;
 void changedSpace(uint64_t reserve=0){std::lock_guard<std::mutex>lock(spaceMutex_);if(known_)free_=reserve>free_?0:free_-reserve;dirty_=true;}
 struct Fd{int value;~Fd(){if(value>=0)::close(value);}};
 static const char*error(){switch(errno){case ENOENT:return "not_found";case EEXIST:return "exists";case ENOSPC:return "no_space";case EROFS:return "read_only";case ENOTDIR:return "not_directory";case EISDIR:return "is_directory";case ENOTEMPTY:return "directory_not_empty";default:return "filesystem_io_failed";}}
 const char*path(const char*p,char*out,size_t n)const{if(background_)return "resource_busy";if(!mounted_)return "provider_unavailable";char clean[256];if(!normalizePath("/",p,clean,sizeof clean))return "invalid_path";size_t a=strlen(base_),b=strlen(clean);if(a+b>=n)return "invalid_path";memcpy(out,base_,a);memcpy(out+a,clean,b+1);return nullptr;}
 struct Guard{std::recursive_mutex*bus;bool held;explicit Guard(std::recursive_mutex*b):bus(b),held(!b||b->try_lock()){}~Guard(){if(bus&&held)bus->unlock();}};
public:
 PosixFileProvider(const char*id,const char*mount,const char*medium,bool removable,bool&mounted,const char*base,const char*resource,const char*filesystem,uint32_t unit,std::recursive_mutex*bus=nullptr):FileProvider(id,mount,medium,true,removable,true),mounted_(mounted),base_(base),resource_(resource),filesystem_(filesystem),unit_(unit),bus_(bus?bus:&localBus_){}
 PosixFileProvider(const PosixFileProvider&)=delete;PosixFileProvider&operator=(const PosixFileProvider&)=delete;
 ~PosixFileProvider(){closeReader();}
 void volume(const char*filesystem,uint32_t unit){std::lock_guard<std::recursive_mutex> guard(*bus_);closeReader();filesystem_=filesystem;unit_=unit;++revision_;known_=false;dirty_=true;}
 void captureSpace(uint64_t total,uint64_t free){std::lock_guard<std::mutex>lock(spaceMutex_);capacity_=total;free_=free;known_=true;dirty_=false;}

 uint64_t maxFileSize()const override{return INT32_MAX;}
 void background(bool active){std::lock_guard<std::recursive_mutex> guard(*bus_);if(active)closeReader();background_=active;}
 void spaceRequester(void*ctx,const char*(*request)(void*)){spaceContext_=ctx;requestSpace_=request;}
 bool busyPath(const char*)const override{return background_;}
 bool available()const override{return mounted_;}bool durableWrites()const override{return true;}const char*operations()const override{return "stat,read,list,write,create,resize,flush,remove,mkdir,rename";}
  uint64_t changeRevision()const override{return revision_.load();}
 void describe(JsonObject o)const override{o["prepare"]["path"]=resource_;o["prepare"]["method"]="mount";o["prepare"]["args"].to<JsonObject>();o["mounted"]=mounted_;o["filesystem"]=filesystem_;o["allocation_unit_bytes"]=unit_;o["growth_overhead_bytes"]=2*unit_;std::lock_guard<std::mutex>lock(spaceMutex_);o["free_is_cached"]=true;o["space_refresh_async"]=requestSpace_!=nullptr;o["free_needs_refresh"]=dirty_;o["capacity_refresh"]["path"]=resource_;o["capacity_refresh"]["method"]="space";o["capacity_refresh"]["args"].to<JsonObject>();if(mounted_&&known_){sizeText(o,"volume_capacity_bytes",capacity_);sizeText(o,"free_bytes",free_);}else{o["volume_capacity_bytes"]=nullptr;o["free_bytes"]=nullptr;}}
 const char*refreshSpace()override{if(!mounted_)return "provider_unavailable";if(background_)return "resource_busy";return requestSpace_?requestSpace_(spaceContext_):"space_executor_unavailable";}
 // Resolve real component spelling for case-folding filesystems, so aliases
 // cannot bypass package digests, NFS identities or active file leases.
 const char*canonicalPath(const char*input,char*out,size_t cap,FileInfo&info)override{
  Guard guard(bus_);if(!guard.held)return "bus_busy";char clean[256],canonical[256]="/";if(!normalizePath("/",input,clean,sizeof clean))return "invalid_path";
  if(auto e=stat("/",info))return e;char*save=nullptr;
  for(char*part=strtok_r(clean,"/",&save);part;part=strtok_r(nullptr,"/",&save)){
   char full[320];if(auto e=path(canonical,full,sizeof full))return e;DIR*dir=opendir(full);if(!dir)return error();char selected[256]{};const char*failure=nullptr;
   for(;;){errno=0;auto entry=readdir(dir);if(!entry){if(errno)failure=error();break;}if(!sameName(part,entry->d_name))continue;if(strlen(entry->d_name)>=sizeof selected){failure="invalid_path";break;}strcpy(selected,entry->d_name);break;}
   closedir(dir);if(failure)return failure;if(!*selected)return "not_found";size_t n=strlen(canonical),z=strlen(selected);if(n+(n>1)+z>=sizeof canonical)return "invalid_path";if(n>1)canonical[n++]='/';memcpy(canonical+n,selected,z+1);
  }
  if(strlen(canonical)>=cap)return "invalid_path";if(auto e=stat(canonical,info))return e;strcpy(out,canonical);return nullptr;
 }
 const char*stat(const char*p,FileInfo&i)override{Guard guard(bus_);if(!guard.held)return "bus_busy";char full[320];if(auto e=path(p,full,sizeof full))return e;struct stat s;if(::stat(full,&s))return error();i={uint64_t(s.st_size),S_ISDIR(s.st_mode)};return nullptr;}
 const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{got=0;Guard guard(bus_);if(!guard.held)return "bus_busy";char full[320];if(auto e=path(p,full,sizeof full)){closeReader();return e;}if(at>INT32_MAX)return "invalid_offset";
  // Alternating a font and an image must not repeatedly reopen the large
  // font. This bounded weak cache owns no VFS lease; every mutation/remount
  // closes all descriptors before effects, under the same physical bus lock.
  Reader*reader=nullptr;for(auto&r:readers_)if(r.fd>=0&&!strcmp(r.path,full)){reader=&r;break;}
  if(!reader){reader=&readers_[0];for(auto&r:readers_)if(r.fd<0||r.used<reader->used)reader=&r;if(reader->fd>=0)::close(reader->fd);*reader={};reader->fd=::open(full,O_RDONLY);if(reader->fd<0)return error();struct stat s;if(fstat(reader->fd,&s)){auto e=error();closeReader();return e;}if(S_ISDIR(s.st_mode)){closeReader();return "is_directory";}memcpy(reader->path,full,strlen(full)+1);}
  reader->used=++readOrder_;if(lseek(reader->fd,at,SEEK_SET)<0){auto e=error();closeReader();return e;}ssize_t r=::read(reader->fd,b,n);if(r<0){auto e=error();closeReader();return e;}got=size_t(r);return nullptr;}

 const char*write(const char*p,uint64_t at,const uint8_t*b,size_t n,bool truncate,size_t&wrote)override{wrote=0;Guard guard(bus_);if(!guard.held)return "bus_busy";if(!writable)return "read_only";if(n>UINT64_MAX-at)return "invalid_offset";if(auto e=admitGrowth(p,at+n))return e;char full[320];if(auto e=path(p,full,sizeof full))return e;if(at>INT32_MAX||(truncate&&at))return "invalid_offset";closeReader();Fd f{::open(full,O_WRONLY|O_CREAT|(truncate?O_TRUNC:0),0600)};if(f.value<0)return error();++revision_;struct stat previous;if(fstat(f.value,&previous))return error();uint64_t before=previous.st_size;if(lseek(f.value,at,SEEK_SET)<0)return error();ssize_t r=::write(f.value,b,n);if(r<0)return error();wrote=size_t(r);uint64_t end=at+wrote;changedSpace(end>before?((end+unit_-1)/unit_-(before+unit_-1)/unit_)*unit_:0);if(wrote!=n)return "write_failed";return fsync(f.value)?error():nullptr;}
 const char*createFile(const char*p)override{Guard guard(bus_);if(!guard.held)return "bus_busy";if(!writable)return "read_only";if(auto e=admitMetadata())return e;char full[320];if(auto e=path(p,full,sizeof full))return e;closeReader();Fd f{::open(full,O_CREAT|O_EXCL|O_RDWR,0600)};if(f.value<0)return error();++revision_;changedSpace(2*unit_);return fsync(f.value)?error():nullptr;}
 const char*resize(const char*p,uint64_t n)override{Guard guard(bus_);if(!guard.held)return "bus_busy";if(!writable)return "read_only";if(auto e=admitGrowth(p,n))return e;char full[320];if(auto e=path(p,full,sizeof full))return e;if(n>INT32_MAX)return "file_size_limit";closeReader();Fd f{::open(full,O_RDWR)};if(f.value<0)return error();struct stat prior;if(fstat(f.value,&prior))return error();if(ftruncate(f.value,n))return error();++revision_;changedSpace(n>uint64_t(prior.st_size)?((n+unit_-1)/unit_-(uint64_t(prior.st_size)+unit_-1)/unit_)*unit_:0);return fsync(f.value)?error():nullptr;}
 const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{Guard guard(bus_);if(!guard.held)return "bus_busy";char full[320];if(auto e=path(p,full,sizeof full))return e;DIR*dir=opendir(full);if(!dir)return error();auto a=o["entries"].to<JsonArray>();uint32_t index=0;bool more=false;const char*failure=nullptr;
  for(;;){errno=0;auto item=readdir(dir);if(!item){if(errno)failure=error();break;}if(!strcmp(item->d_name,".")||!strcmp(item->d_name,".."))continue;if(index++<cursor)continue;if(a.size()>=limit){more=true;break;}char child[576];snprintf(child,sizeof child,"%s/%s",full,item->d_name);struct stat st;if(::stat(child,&st)){failure=error();break;}auto e=a.add<JsonObject>();if(e.isNull()){failure="memory_low";break;}e["name"]=item->d_name;e["directory"]=S_ISDIR(st.st_mode);sizeText(e,"size",st.st_size);}
  closedir(dir);if(failure)return failure;o["next_cursor"]=cursor+a.size();o["more"]=more;return nullptr;}
 const char*remove(const char*p)override{Guard guard(bus_);if(!guard.held)return "bus_busy";if(!writable)return "read_only";char full[320];if(auto e=path(p,full,sizeof full))return e;FileInfo i;if(auto e=stat(p,i))return e;closeReader();if(i.directory ? ::rmdir(full) : ::unlink(full))return error();++revision_;changedSpace();return nullptr;}
 const char*mkdir(const char*p)override{Guard guard(bus_);if(!guard.held)return "bus_busy";if(!writable)return "read_only";if(auto e=admitMetadata())return e;char full[320];if(auto e=path(p,full,sizeof full))return e;closeReader();if(::mkdir(full,0700))return error();++revision_;changedSpace(2*unit_);return nullptr;}
 const char*rename(const char*a,const char*b)override{Guard guard(bus_);if(!guard.held)return "bus_busy";if(!writable)return "read_only";if(auto e=admitMetadata())return e;char from[320],to[320];if(auto e=path(a,from,sizeof from))return e;if(auto e=path(b,to,sizeof to))return e;struct stat exists;if(!::stat(to,&exists))return "exists";if(errno!=ENOENT)return error();closeReader();if(::rename(from,to))return error();++revision_;changedSpace(2*unit_);return nullptr;}
 const char*flush(const char*p)override{Guard guard(bus_);if(!guard.held)return "bus_busy";char full[320];if(auto e=path(p,full,sizeof full))return e;Fd f{::open(full,O_RDONLY)};if(f.value<0)return error();return fsync(f.value)?error():nullptr;}
};
}
