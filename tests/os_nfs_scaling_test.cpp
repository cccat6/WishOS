#include "NfsService.h"
#include <cassert>
#include <cstdio>
#include <iostream>
using namespace notesos;
namespace nf=esp32_nfs;
struct Catalog:FileProvider {
 Catalog():FileProvider("catalog","/catalog","fixture",false,false,false){}
 bool available()const override{return true;}bool caseInsensitive()const override{return true;}
 void describe(JsonObject o)const override{o["volume_capacity_bytes"]="4096";o["free_bytes"]="0";}
 const char*stat(const char*p,FileInfo&i)override{if(!strcmp(p,"/")){i={0,true};return nullptr;}unsigned n;int consumed=0;if(sscanf(p,"/目录-%u.txt%n",&n,&consumed)==1&&p[consumed]==0&&n<1100){i={4,false};return nullptr;}return "not_found";}
 const char*read(const char*p,uint64_t at,uint8_t*b,size_t cap,size_t&got)override{FileInfo i;if(auto e=stat(p,i))return e;got=at<4?std::min<uint64_t>(cap,4-at):0;memset(b,42,got);return nullptr;}
 const char*write(const char*,uint64_t,const uint8_t*,size_t,bool,size_t&)override{return "read_only";}
 const char*list(const char*p,uint32_t cursor,uint32_t limit,JsonObject o)override{if(strcmp(p,"/"))return "not_directory";auto a=o["entries"].to<JsonArray>();uint32_t n=cursor;for(;n<1100&&a.size()<limit;++n){char name[64];snprintf(name,sizeof name,"目录-%u.txt",n);auto item=a.add<JsonObject>();item["name"]=name;item["directory"]=false;item["size"]="4";}o["next_cursor"]=n;o["more"]=n<1100;return nullptr;}
 const char*remove(const char*)override{return "read_only";}const char*mkdir(const char*)override{return "read_only";}const char*rename(const char*,const char*)override{return "read_only";}
};
int main(){Catalog files;Vfs vfs;vfs.add(files);NfsProviderStorage storage(files,vfs);nf::Memory memory({8192,0});nf::PathVfs::Registry registry(memory);NfsProviderVfs fs(storage,files,registry);uint64_t first=0,id=0;unsigned n=0;
 for(;n<1100;++n){char name[64];snprintf(name,sizeof name,"目录-%u.TXT",n);auto status=fs.lookup(fs.root(),name,id);if(status!=nf::Ok){assert(status==nf::Jukebox);break;}if(!n)first=id;}
 assert(n>16&&n<1100&&memory.stats().usedBytes<=8192);auto budgetAt=n;assert(!memory.configure({1,0}));assert(memory.configure({262144,0}));
 for(;n<1100;++n){char name[64];snprintf(name,sizeof name,"目录-%u.TXT",n);assert(fs.lookup(fs.root(),name,id)==nf::Ok);}
 assert(fs.lookup(fs.root(),"目录-0.txt",id)==nf::Ok&&id==first);char canonical[256];assert(fs.canonicalPath(id,canonical,sizeof canonical)==nf::Ok&&!strcmp(canonical,"/目录-0.txt"));assert(fs.identityCount()==1101);
 assert(vfs.lockRead("/catalog/目录-0.TXT",7));assert(!vfs.canMutate("/catalog/目录-0.txt",true));vfs.release(7);
 assert(fs.lookup(fs.root(),"bad\xff",id)==nf::Invalid);auto peak=memory.stats().peakBytes;assert(fs.endSession()==nf::Ok);assert(memory.stats().usedBytes==0&&memory.stats().allocations==0);
 JsonDocument config,out;config["block_size"]=32768;config["memory_max_bytes"]=262144;NfsOptions options;assert(!NfsOptions::parse(config.as<JsonObjectConst>(),options)&&options.wire.blockSize==32768);config["block_size"]=32769;assert(NfsOptions::parse(config.as<JsonObjectConst>(),options));
 std::cout<<"NFS UTF-8/canonical identities: 1100 files, budget refusal at "<<budgetAt<<", peak "<<peak<<" bytes, zero retained allocations after stop\n";
}
