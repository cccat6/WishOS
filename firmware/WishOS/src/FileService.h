#pragma once
#include "Vfs.h"
#include "WireBytes.h"
#include "Policy.h"
namespace notesos {
// One provider contract for CDC, HTTP, scripts and future native ports. Mounting
// and physical link preparation are explicit operations on their own adapters.
class FileService {
 Vfs&vfs_;
 const char*resolve(const char*input,char*full,const char*&relative,FileProvider*&p){if(!normalizePath("/",input,full,256))return "invalid_path";p=vfs_.resolve(full,relative);return p?nullptr:"unknown_provider";}
public:
 static constexpr const char*Methods="mounts,stat,list,read,write,create,resize,flush,remove,mkdir,rename";
 explicit FileService(Vfs&v):vfs_(v){}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out){
  if(!Policies::choice(Methods,m))return "unsupported_method";
  const char*fields=!strcmp(m,"mounts")?"":!strcmp(m,"list")?"file,path,cursor,limit":!strcmp(m,"read")?"file,path,offset,length":!strcmp(m,"write")?"file,path,offset,text,hex,truncate":!strcmp(m,"resize")?"file,path,size":!strcmp(m,"rename")?"file,path,to":"file,path";
  for(auto f:a)if(!Policies::choice(fields,f.key().c_str()))return "unknown_file_argument";
  for(auto key:{"file","path","to","text","hex"})if(a.containsKey(key)&&(!a[key].is<const char*>()||((strcmp(key,"text"))&&a[key].as<JsonString>().size()!=strlen(a[key].as<const char*>()))))return "file_argument_type";
  for(auto key:{"cursor","limit","length"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "file_argument_type";
  if(a.containsKey("truncate")&&!a["truncate"].is<bool>())return "file_argument_type";
  if(!strcmp(m,"mounts")){vfs_.describe(out["providers"].to<JsonArray>());return nullptr;}
  if(!a.containsKey("file")&&!a.containsKey("path"))return "file_path_required";
  if(a.containsKey("file")&&a.containsKey("path")&&a["file"]!=a["path"])return "conflicting_file_paths";
  char full[256];const char*rel;FileProvider*p;if(auto e=resolve(a["file"]|(a["path"]|""),full,rel,p))return e;if(!p->available())return "provider_unavailable";
  uint64_t at=0;if(!parseOffset(a["offset"],at))return "invalid_offset";
  bool mutation=strcmp(m,"stat")&&strcmp(m,"list")&&strcmp(m,"read");Vfs::Access lease(vfs_,mutation);if(auto e=lease.add(full,p->caseInsensitive()))return e;
  if(!strcmp(m,"stat")){FileInfo info;if(auto e=p->stat(rel,info))return e;out["file"]=full;sizeText(out,"size",info.size);out["directory"]=info.directory;return nullptr;}
  if(!strcmp(m,"list")){auto limit=a["limit"]|12u;if(!limit||limit>24)return "list_limit";auto cursor=a["cursor"]|0u;if(auto e=p->list(rel,cursor,limit,out))return e;auto entries=out["entries"].as<JsonArray>();while(entries.size()&&measureJson(out)>MAX_WIRE-256){entries.remove(entries.size()-1);out["more"]=true;out["next_cursor"]=cursor+entries.size();}return nullptr;}
  if(!strcmp(m,"read")){auto length=a["length"]|1024u;if(!length||length>1024)return "chunk_limit";uint8_t bytes[1024];size_t got=0;if(auto e=p->read(rel,at,bytes,length,got))return e;char encoded[2049];hex(bytes,got,encoded);out["hex"]=encoded;sizeText(out,"next_offset",at+got);return nullptr;}
  if(!p->writable)return "read_only";if(p->caseInsensitive()&&strchr(rel,'~'))return "unsupported_short_alias";
  if(!strcmp(m,"write")){if(a.containsKey("text")==a.containsKey("hex"))return "one_file_payload_required";uint8_t bytes[1024];size_t n=0,wrote=0;
   if(a.containsKey("text")){auto text=a["text"].as<JsonString>();n=text.size();if(n>sizeof bytes)return "chunk_limit";memcpy(bytes,text.c_str(),n);}else if(!unhex(a["hex"],bytes,sizeof bytes,n))return "invalid_hex";
   if(auto e=p->write(rel,at,bytes,n,a["truncate"]|false,wrote))return e;out["written"]=wrote;sizeText(out,"next_offset",at+wrote);return nullptr;}
  if(!strcmp(m,"create"))return p->createFile(rel);
  if(!strcmp(m,"resize")){uint64_t size;if(!a.containsKey("size")||!parseOffset(a["size"],size))return "invalid_size";return p->resize(rel,size);}
  if(!strcmp(m,"flush"))return p->flush(rel);
  if(!strcmp(m,"remove"))return p->remove(rel);
  if(!strcmp(m,"mkdir"))return p->mkdir(rel);
  if(!a.containsKey("to"))return "destination_required";char destination[256];const char*dr;FileProvider*dp;if(auto e=resolve(a["to"],destination,dr,dp))return e;if(dp!=p)return "cross_volume_rename";
  if(dp->caseInsensitive()&&strchr(dr,'~'))return "unsupported_short_alias";if(auto e=lease.add(destination,dp->caseInsensitive()))return e;return p->rename(rel,dr);
 }
};
}
