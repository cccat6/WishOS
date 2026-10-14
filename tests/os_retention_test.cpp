#include "RecordingRetention.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static void write(MemoryFiles&f,const char*p,size_t n){uint8_t bytes[256]{};size_t wrote;assert(!f.write(p,0,bytes,n,true,wrote));}
int main(){MemoryFiles files("test","/store","ram");assert(!files.mkdir("/recordings"));Vfs v;v.add(files);RecordingRetention r(files,v);r.configure({300,2,true});
 for(int n=0;n<3;++n){char path[64];snprintf(path,sizeof path,"/recordings/r%d.adpcm",n);write(files,path,80);std::string idx=std::string(path)+".idx",meta=std::string(path)+".json";write(files,idx.c_str(),10);write(files,meta.c_str(),10);assert(!r.remember(path,100,n+1));}
 assert(v.lockRead("/store/recordings/r0.adpcm",1));assert(r.makeRoom(50,1));FileInfo i;assert(!files.stat("/recordings/r0.adpcm",i));v.release(1);assert(!r.makeRoom(50,1));assert(files.stat("/recordings/r0.adpcm",i));assert(files.stat("/recordings/r1.adpcm",i));assert(!files.stat("/recordings/r2.adpcm",i));assert(r.nextOrder()==4);assert(r.makeRoom(301,1));
 r.configure({100,1,false});assert(r.makeRoom(1,1));assert(!files.stat("/recordings/r2.adpcm",i));assert(r.remember("/unrelated.adpcm",1));
 // Retention observes the same root-mount leases as file service and NFS.
 {MemoryFiles root("root","/","ram");Vfs rootVfs;rootVfs.add(root);assert(!root.mkdir("/recordings"));
  RecordingRetention history(root,rootVfs);history.configure({100,1,true});
  write(root,"/recordings/old.adpcm",80);assert(!history.remember("/recordings/old.adpcm",80));
  assert(rootVfs.lockRead("/recordings/old.adpcm",27));assert(history.makeRoom(30,1));
  FileInfo old;assert(!root.stat("/recordings/old.adpcm",old));rootVfs.release(27);assert(!history.makeRoom(30,1));
 }
 Core c;size_t free=0;auto&budget=c.resources();auto one=budget.acquire(32768,1);assert(one);budget.used(one,16384);auto two=budget.acquire(32768,2);assert(two);JsonDocument limits,out;limits["memory_limit_bytes"]=100;assert(budget.configure(limits.as<JsonObjectConst>()));budget.read(out.to<JsonObject>());assert(out["reserved_bytes"]==65536&&out["unallocated_reserved_bytes"]==49152);budget.release(one);budget.release(two);assert(!budget.configure(limits.as<JsonObjectConst>()));assert(!budget.acquire(101,3));
 std::cout<<"Recording caps, oldest deletion, active-file leases, stop-storage policy and shared reservations passed\n";
}
