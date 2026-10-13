#pragma once
#include "Vfs.h"
#include <LittleFS.h>
namespace notesos {
class LittleFsProvider:public FileProvider {
 fs::LittleFSFS&fs_;bool&mounted_;uint64_t revision_=1;
public:
 LittleFsProvider(fs::LittleFSFS&fs,bool&mounted):FileProvider("flash","/storage/flash","internal_flash",true,false,true),fs_(fs),mounted_(mounted){}
 bool available()const override{return mounted_;}bool durableWrites()const override{return true;}uint64_t changeRevision()const override{return revision_;}
 void describe(JsonObject o)const override{o["mounted"]=mounted_;o["filesystem"]="littlefs";if(mounted_){sizeText(o,"volume_capacity_bytes",fs_.totalBytes());sizeText(o,"free_bytes",fs_.totalBytes()-fs_.usedBytes());}}
 const char*stat(const char*p,FileInfo&i)override{if(!mounted_)return "provider_unavailable";auto f=fs_.open(p,"r");if(!f)return "not_found";i={f.size(),f.isDirectory()};return nullptr;}
 const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{got=0;if(!mounted_)return "provider_unavailable";auto f=fs_.open(p,"r");if(!f)return "not_found";if(f.isDirectory())return "is_directory";if(at>=f.size())return nullptr;if(at>UINT32_MAX||!f.seek(at))return "seek_failed";got=f.read(b,n);return !got&&n?"read_failed":nullptr;}
 const char*write(const char*p,uint64_t at,const uint8_t*b,size_t n,bool truncate,size_t&wrote)override{wrote=0;if(!mounted_)return "provider_unavailable";if(at>UINT32_MAX)return "invalid_offset";bool exists=fs_.exists(p);auto f=fs_.open(p,truncate||!exists?"w":"r+");if(!f||!f.seek(at))return "open_failed";wrote=f.write(b,n);f.flush();++revision_;return wrote==n&&!f.getWriteError()?nullptr:"write_failed";}
 const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{if(!mounted_)return "provider_unavailable";auto dir=fs_.open(p,"r");if(!dir||!dir.isDirectory())return "not_directory";auto a=o["entries"].to<JsonArray>();uint32_t index=0;bool more=false;for(auto f=dir.openNextFile();f;f=dir.openNextFile()){if(index++<cursor)continue;if(a.size()>=limit){more=true;break;}auto e=a.add<JsonObject>();const char*name=f.name();if(auto slash=strrchr(name,'/'))name=slash+1;e["name"]=name;e["directory"]=f.isDirectory();sizeText(e,"size",f.size());}o["next_cursor"]=cursor+a.size();o["more"]=more;return o["entries"].isNull()?"memory_low":nullptr;}
 const char*remove(const char*p)override{FileInfo i;if(auto e=stat(p,i))return e;bool ok=i.directory?fs_.rmdir(p):fs_.remove(p);if(ok)++revision_;return ok?nullptr:"remove_failed";}
 const char*mkdir(const char*p)override{if(!mounted_)return "provider_unavailable";if(!fs_.mkdir(p))return "mkdir_failed";++revision_;return nullptr;}
 const char*rename(const char*a,const char*b)override{if(!mounted_)return "provider_unavailable";if(fs_.exists(b))return "exists";if(!fs_.rename(a,b))return "rename_failed";++revision_;return nullptr;}
 const char*flush(const char*p)override{auto f=fs_.open(p,"r");if(!f)return "not_found";f.flush();return f.getWriteError()?"sync_failed":nullptr;}
};
}
