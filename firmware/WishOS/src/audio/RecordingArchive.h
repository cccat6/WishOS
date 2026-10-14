#pragma once
#include "../Core.h"
#include "../Vfs.h"
#include "../WireBytes.h"
namespace notesos::audio {
// Random access to independently compressed blocks. Offsets live in a small
// fixed-entry sidecar written during capture; reopening never scans the audio.
class RecordingArchive {
 Vfs&vfs_;
 static uint64_t le(const uint8_t*p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;++i)v|=uint64_t(p[i])<<(8*i);return v;}
 static uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(unsigned i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
 static const char*exact(FileProvider&f,const char*p,uint64_t at,uint8_t*out,size_t n){size_t got=0;if(auto e=f.read(p,at,out,n,got))return e;return got==n?nullptr:"recording_truncated";}
 static void base64(const uint8_t*p,size_t n,char*out){static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t j=0;for(size_t i=0;i<n;i+=3){uint32_t b=uint32_t(p[i])<<16;if(i+1<n)b|=uint32_t(p[i+1])<<8;if(i+2<n)b|=p[i+2];out[j++]=t[(b>>18)&63];out[j++]=t[(b>>12)&63];out[j++]=i+1<n?t[(b>>6)&63]:'=';out[j++]=i+2<n?t[b&63]:'=';}out[j]=0;}
public:
 explicit RecordingArchive(Vfs&v):vfs_(v){}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){
  bool reading=!strcmp(m,"read_chunk"),removing=!strcmp(m,"remove");if(!reading&&!removing&&strcmp(m,"read"))return "unsupported_method";
  for(auto f:a)if(strcmp(f.key().c_str(),"file")&&(!reading||!Core::containsMethod("cursor,limit",f.key().c_str())))return "unknown_recording_argument";
  if(!a["file"].is<const char*>()||a["file"].as<JsonString>().size()!=strlen(a["file"].as<const char*>()))return "recording_file_required";
  for(auto key:{"cursor","limit"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "recording_range";
  unsigned limit=a["limit"]|8u;uint32_t cursor=a["cursor"]|0u;if(!limit||limit>16)return "recording_range";
  char full[256],data[256],index[256],metadata[256];if(!normalizePath("/",a["file"],full,sizeof full))return "invalid_path";const char*relative=nullptr;auto f=vfs_.resolve(full,relative);if(!f||!f->available())return "storage_unavailable";
  size_t length=strlen(relative);if(length<7||strcmp(relative+length-6,".adpcm")||length+6>=sizeof data)return "recording_file_format";
  strcpy(data,relative);snprintf(index,sizeof index,"%s.idx",data);snprintf(metadata,sizeof metadata,"%s.json",data);
  Vfs::Access lease(vfs_,removing);for(auto path:{data,index,metadata}){char name[256];if(!f->absolutePath(path,name,sizeof name))return "invalid_path";if(auto e=lease.add(name,f->caseInsensitive()))return e;if(f->busyPath(path))return "recording_in_use";}
  FileInfo info;if(auto e=f->stat(metadata,info))return e;if(info.directory||info.size>1024)return "recording_metadata";uint8_t text[1025];if(auto e=exact(*f,metadata,0,text,info.size))return e;JsonDocument meta;if(deserializeJson(meta,text,info.size)||meta["format"]!="notes-recording/1"||meta["index"]!="nai1-le-32")return "recording_metadata";
  if(removing){for(auto p:{metadata,index,data})if(auto e=f->remove(p))if(strcmp(e,"not_found"))return e;o["removed"]=true;return nullptr;}
  o["metadata"]=meta;o["file"]=full;if(!reading)return nullptr;
  FileInfo idx,pcm;if(auto e=f->stat(index,idx))return e;if(auto e=f->stat(data,pcm))return e;uint8_t head[16];if(auto e=exact(*f,index,0,head,16))return e;if(memcmp(head,"NAI1",4)||le(head+8,4)!=32||idx.size<16||(idx.size-16)%32)return "recording_index";
  uint64_t count=(idx.size-16)/32;if(count>UINT32_MAX||cursor>count)return "recording_cursor";
  auto chunks=o["chunks"].to<JsonArray>();uint32_t next=cursor;
  for(;next<count&&chunks.size()<limit;++next){uint8_t entry[32],packet[651];if(auto e=exact(*f,index,16+uint64_t(next)*32,entry,32))return e;auto offset=le(entry+12,8);auto bytes=le(entry+20,4);if(bytes<12||bytes>sizeof packet||offset>pcm.size||bytes>pcm.size-offset)return "recording_index";
   if(auto e=exact(*f,data,offset,packet,bytes))return e;if(crc(packet,bytes)!=le(entry+24,4)||packet[0]!=0x42||le(packet+1,4)!=le(entry,4)||packet[7]>88||le(packet+9,2)<1||11+(le(packet+9,2)+1)/2!=bytes)return "recording_block";
   char encoded[869];base64(packet,bytes,encoded);auto item=chunks.add<JsonObject>();item["sequence"]=le(entry,4);item["samples"]=le(packet+9,2);sizeText(item,"sample_start",le(entry+4,8));item["adpcm_b64"]=encoded;
   if(measureJson(o)>MAX_WIRE-512){chunks.remove(chunks.size()-1);break;}
  }
  o["cursor"]=cursor;o["next_cursor"]=next;o["more"]=next<count;return nullptr;
 }
 static void describe(JsonObject o){o["format"]="notes-recording/1";o["index"]="nai1-le-32";o["random_block_access"]=true;o["read_only_methods"]="read,read_chunk";o["remove_group"]="metadata,index,compressed_audio";o["requires"]="prepared_file_provider";}
};
}
