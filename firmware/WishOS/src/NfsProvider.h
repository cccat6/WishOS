#pragma once
#include "Vfs.h"
#include <ESP32NFS.h>
#include <backends/PathVfs.h>
#include <limits.h>
namespace notesos {
inline esp32_nfs::Status nfsError(const char*e){
 using namespace esp32_nfs;if(!e)return Ok;
 if(!strcmp(e,"not_found"))return NoEnt;
 if(!strcmp(e,"read_only"))return ReadOnly;
 if(!strcmp(e,"exists"))return Exist;
 if(!strcmp(e,"not_directory"))return NotDir;
 if(!strcmp(e,"is_directory"))return IsDir;
 if(!strcmp(e,"directory_not_empty"))return NotEmpty;
 if(!strcmp(e,"memory_low")||!strcmp(e,"file_lease_capacity"))return Jukebox;
 if(!strcmp(e,"quota_exceeded")||!strcmp(e,"entry_capacity"))return NoSpace;
 if(!strcmp(e,"unsupported_operation")||!strcmp(e,"directory_rename_not_supported"))return NotSupported;
 if(!strcmp(e,"file_in_use")||!strcmp(e,"bus_busy")||!strcmp(e,"spi_busy")||!strcmp(e,"resource_busy"))return Jukebox;
 if(!strcmp(e,"provider_unavailable"))return Stale;
 return Io;
}
// Uses OS providers, never opens a second SD/FAT stack. No persistent open handles.
class NfsProviderStorage:public esp32_nfs::PathStorage {
 FileProvider&provider_;Vfs&vfs_;uint64_t observed_=0,epoch_=1;bool externalPending_=false;
 const char*guard(const char*p,bool mutation)const{
   if(!provider_.available())return "provider_unavailable";
   if(provider_.caseInsensitive()&&strchr(p,'~'))return "unsupported_operation"; // FAT short aliases must not bypass path leases.
   if(mutation&&!provider_.writable)return "read_only";
   if(provider_.busyPath(p))return "file_in_use";
   if(mutation){char full[256];if(!provider_.absolutePath(p,full,sizeof full))return "invalid_path";
     if(!vfs_.canMutate(full,provider_.caseInsensitive()))return "file_in_use";}
   return nullptr;
 }
 const char*leasePath(Vfs::Access&lease,const char*p){char full[256];if(!provider_.absolutePath(p,full,sizeof full))return "invalid_path";return lease.add(full,provider_.caseInsensitive());}
 esp32_nfs::Status changed(const char*e){auto now=provider_.changeRevision();
   // More than this operation's single revision can include a concurrent recording commit.
   if(now!=observed_&&(e||now!=observed_+1))externalPending_=true;
   observed_=now;return nfsError(e);
 }
public:
 NfsProviderStorage(FileProvider&p,Vfs&v):provider_(p),vfs_(v),observed_(p.changeRevision()){}
 // External writers cannot silently reuse a previously issued remote file identity.
 bool reconcile(){auto now=provider_.changeRevision();if(now==observed_&&!externalPending_)return false;observed_=now;externalPending_=false;++epoch_;return true;}
 uint64_t generation()const override{return epoch_;}
 bool writable()const override{return provider_.writable;}
 bool caseInsensitive()const override{return provider_.caseInsensitive();}
 bool sameName(const char*a,const char*b)const override{return provider_.sameName(a,b);}
 esp32_nfs::Status validateName(const char*name)const override{return esp32_nfs::name_detail::component(name,provider_.caseInsensitive());}
 esp32_nfs::Status resolve(const char*input,char*out,size_t cap,esp32_nfs::Attr&a)override{
   using namespace esp32_nfs;if(auto status=name_detail::path(input,provider_.caseInsensitive()))return status;
   if(auto e=guard(input,false))return nfsError(e);Vfs::Access access(vfs_,false);if(auto e=leasePath(access,input))return nfsError(e);char wanted[PathSize];if(strlen(input)>=sizeof wanted)return NameTooLong;strcpy(wanted,input);
   char canonical[PathSize]="/";
   if(provider_.caseInsensitive()){
     FileInfo info;auto error=provider_.canonicalPath(input,canonical,sizeof canonical,info);
     if(!error){if(auto e=guard(canonical,false))return nfsError(e);if(strlen(canonical)>=cap)return NameTooLong;
       a={};a.type=info.directory?2:1;a.mode=info.directory?0777:0666;if(!writable())a.mode&=~0222;a.size=a.used=info.size;memmove(out,canonical,strlen(canonical)+1);return Ok;}
     if(strcmp(error,"unsupported_operation"))return nfsError(error);
     char*save=nullptr;for(char*component=strtok_r(wanted,"/",&save);component;component=strtok_r(nullptr,"/",&save)){
       bool found=false;uint32_t cursor=0;
       do{JsonDocument list;if(auto e=provider_.list(canonical,cursor,12,list.to<JsonObject>()))return nfsError(e);if(list.overflowed())return Jukebox;
         for(auto entry:list["entries"].as<JsonArrayConst>()){const char*name=entry["name"]|"";if(provider_.sameName(component,name)){
           if(name_detail::component(name,true)!=Ok)return Io;size_t at=strlen(canonical),n=strlen(name);bool sep=at>1;if(at+sep+n>=sizeof canonical)return NameTooLong;if(sep)canonical[at++]='/';memcpy(canonical+at,name,n+1);found=true;break;}}
         if(found)break;if(!(list["more"]|false))return NoEnt;uint32_t next=list["next_cursor"]|0u;if(next<=cursor)return Io;cursor=next;
       }while(true);
     }
   }else strcpy(canonical,input);
   if(strlen(canonical)>=cap)return NameTooLong;auto status=stat(canonical,a);if(status==Ok)memmove(out,canonical,strlen(canonical)+1);return status;
 }
 uint64_t maxFileSize()const override{return provider_.maxFileSize();}
 esp32_nfs::Status stat(const char*p,esp32_nfs::Attr&a)override{
   if(auto e=guard(p,false))return nfsError(e);Vfs::Access access(vfs_,false);if(auto e=leasePath(access,p))return nfsError(e);FileInfo i;if(auto e=provider_.stat(p,i))return nfsError(e);
   a={};a.type=i.directory?2:1;a.mode=i.directory?0777:0666;if(!writable())a.mode&=~0222;a.size=a.used=i.size;return esp32_nfs::Ok;
 }
 esp32_nfs::Status read(const char*p,uint64_t off,uint8_t*b,size_t&n)override{
   if(auto e=guard(p,false))return nfsError(e);Vfs::Access access(vfs_,false);if(auto e=leasePath(access,p))return nfsError(e);size_t got=0;auto e=provider_.read(p,off,b,n,got);n=got;return nfsError(e);
 }
 esp32_nfs::Status write(const char*p,uint64_t off,const uint8_t*b,size_t&n)override{
   if(auto e=guard(p,true))return nfsError(e);Vfs::Access access(vfs_,true);if(auto e=leasePath(access,p))return nfsError(e);FileInfo i;if(auto e=provider_.stat(p,i))return nfsError(e);
   if(i.directory)return esp32_nfs::IsDir;
   // Avoid an unbounded sparse-hole allocation in a cooperative storage RPC.
   if(off>i.size)return esp32_nfs::NotSupported;
   size_t wrote=0;auto e=provider_.write(p,off,b,n,false,wrote);n=wrote;return changed(e);
 }
 esp32_nfs::Status setattr(const char*p,const esp32_nfs::SetAttr&a)override{
   if(auto e=guard(p,true))return nfsError(e);Vfs::Access access(vfs_,true);if(auto e=leasePath(access,p))return nfsError(e);
   if(a.uidSet||a.gidSet||a.atimeHow||a.mtimeHow)return esp32_nfs::NotSupported;
   // Permissions are defined by the export and provider, not a fake Unix metadata file.
   if(a.modeSet&&(a.mode&0777)!=0666&&(a.mode&0777)!=0777)return esp32_nfs::NotSupported;
   if(!a.sizeSet)return esp32_nfs::Ok;
   return changed(provider_.resize(p,a.size));
 }
 esp32_nfs::Status create(const char*p,bool dir)override{
   if(auto e=guard(p,true))return nfsError(e);Vfs::Access access(vfs_,true);if(auto e=leasePath(access,p))return nfsError(e);FileInfo i;auto e=provider_.stat(p,i);
   if(!e)return esp32_nfs::Exist;if(strcmp(e,"not_found"))return nfsError(e);
   if(dir)return changed(provider_.mkdir(p));
   return changed(provider_.createFile(p));
 }
 esp32_nfs::Status remove(const char*p,bool dir)override{
   if(!strcmp(p,"/"))return esp32_nfs::Access;if(auto e=guard(p,true))return nfsError(e);Vfs::Access access(vfs_,true);if(auto e=leasePath(access,p))return nfsError(e);
   FileInfo i;if(auto e=provider_.stat(p,i))return nfsError(e);
   if(dir!=i.directory)return dir?esp32_nfs::NotDir:esp32_nfs::IsDir;
   return changed(provider_.remove(p));
 }
 esp32_nfs::Status rename(const char*a,const char*b)override{
   if(!strcmp(a,"/")||!strcmp(b,"/"))return esp32_nfs::Access;
   if(auto e=guard(a,true))return nfsError(e);if(auto e=guard(b,true))return nfsError(e);Vfs::Access access(vfs_,true);if(auto e=leasePath(access,a))return nfsError(e);if(auto e=leasePath(access,b))return nfsError(e);
   FileInfo info;auto e=provider_.stat(b,info);if(!e)return esp32_nfs::NotSupported;
   if(strcmp(e,"not_found"))return nfsError(e);return changed(provider_.rename(a,b));
 }
 esp32_nfs::Status entry(const char*p,uint64_t index,char*name,size_t cap,bool&eof)override{
   if(index>UINT32_MAX)return esp32_nfs::BadCookie;if(auto e=guard(p,false))return nfsError(e);Vfs::Access access(vfs_,false);if(auto e=leasePath(access,p))return nfsError(e);
   JsonDocument d;if(auto e=provider_.list(p,uint32_t(index),1,d.to<JsonObject>()))return nfsError(e);
   auto entries=d["entries"].as<JsonArrayConst>();eof=!entries.size();if(eof)return esp32_nfs::Ok;
   const char*s=entries[0]["name"]|"";if(!*s||strlen(s)>=cap)return esp32_nfs::NameTooLong;
   strcpy(name,s);return esp32_nfs::Ok;
 }
 esp32_nfs::Status sync(const char*p)override{if(auto e=guard(p,false))return nfsError(e);Vfs::Access access(vfs_,false);if(auto e=leasePath(access,p))return nfsError(e);return nfsError(provider_.flush(p));}
 esp32_nfs::Status space(esp32_nfs::Space&s)override{
   JsonDocument d;provider_.describe(d.to<JsonObject>());uint64_t total=0,free=0;
   if(d["free_bytes"].isNull()&&d["quota_free_bytes"].isNull()){
     if(auto e=provider_.refreshSpace())return nfsError(e);
     d.clear();provider_.describe(d.to<JsonObject>());
   }
   if((d["free_needs_refresh"]|false)&&(d["space_refresh_async"]|false))provider_.refreshSpace();
   auto t=d["volume_capacity_bytes"].isNull()?d["quota_bytes"]:d["volume_capacity_bytes"];
   auto f=d["free_bytes"].isNull()?d["quota_free_bytes"]:d["free_bytes"];
   if(t.isNull()||f.isNull()||!parseOffset(t,total)||!parseOffset(f,free))return esp32_nfs::Jukebox;
   s={};s.totalBytes=total;s.freeBytes=free;return esp32_nfs::Ok;
 }
};
class NfsProviderVfs:public esp32_nfs::PathVfs {
 FileProvider&provider_;
public:
 NfsProviderVfs(NfsProviderStorage&s,FileProvider&p):PathVfs(s),provider_(p){}
 NfsProviderVfs(NfsProviderStorage&s,FileProvider&p,Registry&r):PathVfs(s,r),provider_(p){}
 bool stableStorage()const override{return provider_.durableWrites();}
};
}
