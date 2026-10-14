#pragma once
#include "Vfs.h"
#include <LittleFS.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
namespace notesos {
// The native VFS supplies exclusive create, truncate and error-preserving I/O;
// Arduino File's reduced API cannot implement the same provider contract.
class LittleFsProvider:public FileProvider {
 fs::LittleFSFS&fs_;bool&mounted_;const char*base_;uint64_t revision_=1;
 struct Fd{int value;~Fd(){if(value>=0)::close(value);}};
 static const char*error(){switch(errno){case ENOENT:return "not_found";case EEXIST:return "exists";case ENOSPC:return "no_space";case EROFS:return "read_only";case ENOTDIR:return "not_directory";case EISDIR:return "is_directory";case ENOTEMPTY:return "directory_not_empty";default:return "filesystem_io_failed";}}
 const char*path(const char*p,char*out,size_t n)const{if(!mounted_)return "provider_unavailable";char clean[256];if(!normalizePath("/",p,clean,sizeof clean)||snprintf(out,n,"%s%s",base_,clean)>=int(n))return "invalid_path";return nullptr;}
public:
 LittleFsProvider(fs::LittleFSFS&fs,bool&mounted,const char*base):FileProvider("flash","/storage/flash","internal_flash",true,false,true),fs_(fs),mounted_(mounted),base_(base){}
 uint64_t maxFileSize()const override{return INT32_MAX;}
 bool available()const override{return mounted_;}bool durableWrites()const override{return true;}const char*operations()const override{return "stat,read,list,write,create,resize,flush,remove,mkdir,rename";}
  uint64_t changeRevision()const override{return revision_;}
 void describe(JsonObject o)const override{o["prepare"]["path"]="/dev/flash";o["prepare"]["method"]="mount";o["prepare"]["args"].to<JsonObject>();o["mounted"]=mounted_;o["filesystem"]="littlefs";if(mounted_){sizeText(o,"volume_capacity_bytes",fs_.totalBytes());sizeText(o,"free_bytes",fs_.totalBytes()-fs_.usedBytes());}}
 const char*stat(const char*p,FileInfo&i)override{char full[320];if(auto e=path(p,full,sizeof full))return e;struct stat s;if(::stat(full,&s))return error();i={uint64_t(s.st_size),S_ISDIR(s.st_mode)};return nullptr;}
 const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{got=0;char full[320];if(auto e=path(p,full,sizeof full))return e;if(at>INT32_MAX)return "invalid_offset";Fd f{::open(full,O_RDONLY)};if(f.value<0)return error();struct stat s;if(fstat(f.value,&s))return error();if(S_ISDIR(s.st_mode))return "is_directory";if(lseek(f.value,at,SEEK_SET)<0)return error();ssize_t r=::read(f.value,b,n);if(r<0)return error();got=size_t(r);return nullptr;}
 const char*write(const char*p,uint64_t at,const uint8_t*b,size_t n,bool truncate,size_t&wrote)override{wrote=0;char full[320];if(auto e=path(p,full,sizeof full))return e;if(at>INT32_MAX||(truncate&&at))return "invalid_offset";Fd f{::open(full,O_WRONLY|O_CREAT|(truncate?O_TRUNC:0),0600)};if(f.value<0)return error();++revision_;if(lseek(f.value,at,SEEK_SET)<0)return error();ssize_t r=::write(f.value,b,n);if(r<0)return error();wrote=size_t(r);if(wrote!=n)return "write_failed";return fsync(f.value)?error():nullptr;}
 const char*createFile(const char*p)override{char full[320];if(auto e=path(p,full,sizeof full))return e;Fd f{::open(full,O_CREAT|O_EXCL|O_RDWR,0600)};if(f.value<0)return error();++revision_;return fsync(f.value)?error():nullptr;}
 const char*resize(const char*p,uint64_t n)override{char full[320];if(auto e=path(p,full,sizeof full))return e;if(n>INT32_MAX)return "file_size_limit";Fd f{::open(full,O_RDWR)};if(f.value<0)return error();if(ftruncate(f.value,n))return error();++revision_;return fsync(f.value)?error():nullptr;}
 const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{char full[320];if(auto e=path(p,full,sizeof full))return e;DIR*dir=opendir(full);if(!dir)return error();auto a=o["entries"].to<JsonArray>();uint32_t index=0;bool more=false;const char*failure=nullptr;
  for(;;){errno=0;auto item=readdir(dir);if(!item){if(errno)failure=error();break;}if(!strcmp(item->d_name,".")||!strcmp(item->d_name,".."))continue;if(index++<cursor)continue;if(a.size()>=limit){more=true;break;}char child[576];snprintf(child,sizeof child,"%s/%s",full,item->d_name);struct stat st;if(::stat(child,&st)){failure=error();break;}auto e=a.add<JsonObject>();if(e.isNull()){failure="memory_low";break;}e["name"]=item->d_name;e["directory"]=S_ISDIR(st.st_mode);sizeText(e,"size",st.st_size);}
  closedir(dir);if(failure)return failure;o["next_cursor"]=cursor+a.size();o["more"]=more;return nullptr;}
 const char*remove(const char*p)override{char full[320];if(auto e=path(p,full,sizeof full))return e;FileInfo i;if(auto e=stat(p,i))return e;if(i.directory ? ::rmdir(full) : ::unlink(full))return error();++revision_;return nullptr;}
 const char*mkdir(const char*p)override{char full[320];if(auto e=path(p,full,sizeof full))return e;if(::mkdir(full,0700))return error();++revision_;return nullptr;}
 const char*rename(const char*a,const char*b)override{char from[320],to[320];if(auto e=path(a,from,sizeof from))return e;if(auto e=path(b,to,sizeof to))return e;struct stat exists;if(!::stat(to,&exists))return "exists";if(errno!=ENOENT)return error();if(::rename(from,to))return error();++revision_;return nullptr;}
 const char*flush(const char*p)override{char full[320];if(auto e=path(p,full,sizeof full))return e;Fd f{::open(full,O_RDONLY)};if(f.value<0)return error();return fsync(f.value)?error():nullptr;}
};
}
