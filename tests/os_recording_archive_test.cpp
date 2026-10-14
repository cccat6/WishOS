#include "audio/RecordingArchive.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static void le(uint8_t*p,uint64_t v,unsigned n){while(n--){*p++=v;v>>=8;}}
static uint32_t crc(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
int main(){Vfs vfs;MemoryFiles files("records","/card","ram");vfs.add(files);audio::RecordingArchive archive(vfs);uint8_t audio[182]{},idx[80]{};memcpy(idx,"NAI1",4);le(idx+4,16000,4);le(idx+8,32,4);
 for(unsigned i=0;i<2;++i){auto p=audio+i*91;p[0]=0x42;le(p+1,i+3,4);le(p+9,160,2);auto e=idx+16+i*32;le(e,i+3,4);le(e+4,(i+3)*160,8);le(e+12,i*91,8);le(e+20,91,4);le(e+24,crc(p,91),4);}
 size_t got;assert(!files.write("/clip.adpcm",0,audio,sizeof audio,true,got));assert(!files.write("/clip.adpcm.idx",0,idx,sizeof idx,true,got));const char*meta=R"({"format":"notes-recording/1","index":"nai1-le-32","produced_samples":"800","sample_rate_hz":16000,"capture_failed":false,"retention":"history"})";assert(!files.write("/clip.adpcm.json",0,(const uint8_t*)meta,strlen(meta),true,got));
 JsonDocument a,o;a["file"]="/card/clip.adpcm";a["cursor"]=1;a["limit"]=1;assert(!archive.invoke("read_chunk",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(o["chunks"].size()==1&&o["chunks"][0]["sequence"]==4&&o["chunks"][0]["sample_start"]=="640"&&!o["more"].as<bool>());
 audio[100]^=1;assert(!files.write("/clip.adpcm",0,audio,sizeof audio,true,got));assert(!strcmp(archive.invoke("read_chunk",a.as<JsonObjectConst>(),o.to<JsonObject>()),"recording_block"));
 a.remove("cursor");a.remove("limit");assert(vfs.lockRead("/card/clip.adpcm.idx",3));assert(!strcmp(archive.invoke("remove",a.as<JsonObjectConst>(),o.to<JsonObject>()),"file_in_use"));vfs.release(3);assert(!archive.invoke("remove",a.as<JsonObjectConst>(),o.to<JsonObject>()));FileInfo info;assert(files.stat("/clip.adpcm",info));assert(files.stat("/clip.adpcm.idx",info));assert(files.stat("/clip.adpcm.json",info));
 std::cout<<"Indexed compressed recording access, corruption rejection and grouped leased deletion passed\n";
}
