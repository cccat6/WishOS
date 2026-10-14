#include "NfsService.h"
#include "compositions/agentic-notes-e6/Defaults.h"
#include <Xdr.h>
#include <cassert>
#include <vector>
#include <iostream>
using namespace notesos;
namespace nf=esp32_nfs;
struct DurableTestFiles:MemoryFiles {
 DurableTestFiles():MemoryFiles("user","/storage/user","test"){}
 bool durableWrites()const override{return true;} // Host test fixture, not a claim about real RAM.
};
struct Transport:nf::Transport {
 bool listening=false,accepted=false,alive=false,failListen=false;
 std::vector<uint8_t> input,output;size_t at=0;
 bool listen(uint16_t)override{return listening=!failListen;}
 int accept(uint64_t&peer)override{if(accepted||input.empty())return -1;accepted=alive=true;peer=1;return 1;}
 bool connected(int)override{return alive;}
 int available(int)override{return input.size()-at;}
 int read(int,uint8_t*p,size_t n)override{n=std::min(n,input.size()-at);memcpy(p,input.data()+at,n);at+=n;return n;}
 int write(int,const uint8_t*p,size_t n)override{output.insert(output.end(),p,p+n);return n;}
 void close(int)override{alive=false;}
 void stop()override{listening=alive=false;}
 void submit(const std::vector<uint8_t>&rpc){output.clear();input.clear();at=0;uint32_t n=rpc.size()|0x80000000u;
   for(int b=24;b>=0;b-=8)input.push_back(uint8_t(n>>b));input.insert(input.end(),rpc.begin(),rpc.end());}
};
static std::vector<uint8_t> call(uint32_t xid,uint32_t program,uint32_t proc,const std::vector<uint8_t>&args){
 uint8_t b[40000];nf::XdrOut x(b,sizeof b);x.u32(xid);x.u32(0);x.u32(2);x.u32(program);x.u32(3);x.u32(proc);
 x.u32(0);x.u32(0);x.u32(0);x.u32(0);x.fixed(args.data(),args.size());assert(x.valid);return {b,b+x.pos};
}
template<class Fn>static std::vector<uint8_t> args(Fn fn){uint8_t b[40000];nf::XdrOut x(b,sizeof b);fn(x);assert(x.valid);return {b,b+x.pos};}
static std::vector<uint8_t> transact(NfsService&s,Transport&t,const std::vector<uint8_t>&rpc,uint32_t&now){
 t.submit(rpc);for(int k=0;k<1000;++k){s.poll(++now);if(t.output.size()>=4){uint32_t n=0;for(int i=0;i<4;++i)n=(n<<8)|t.output[i];n&=0x7fffffff;if(t.output.size()==n+4)return {t.output.begin()+4,t.output.end()};}}
 assert(false&&"RPC did not finish");return {};
}
static nf::XdrIn reply(const std::vector<uint8_t>&b,uint32_t xid){nf::XdrIn x(b.data(),b.size());assert(x.u32()==xid);assert(x.u32()==1);assert(x.u32()==0);assert(x.u32()==0);assert(x.u32()==0);assert(x.u32()==0);return x;}
int main(){
 DurableTestFiles files;size_t n;uint8_t text[]={1,2,3,4};assert(!files.write("/hello",0,text,4,false,n));
 Vfs vfs;assert(vfs.add(files));NfsProviderStorage storage(files,vfs);NfsProviderVfs mapped(storage,files);
 uint64_t id;assert(mapped.lookup(mapped.root(),"hello",id)==nf::Ok);
 assert(vfs.lockRead("/storage/user/hello",7));assert(!vfs.canMutate("/storage/USER/HELLO",true));n=4;assert(mapped.write(id,0,text,n)==nf::Jukebox);vfs.release(7);
 nf::SetAttr attr;attr.sizeSet=true;attr.size=2;assert(mapped.setattr(id,attr)==nf::Ok);FileInfo fi;files.stat("/hello",fi);assert(fi.size==2);
 uint64_t made=0;nf::SetAttr empty;
 assert(mapped.create(mapped.root(),"made",false,1,0,empty,made)==nf::Ok);
 assert(mapped.create(mapped.root(),"made",false,1,0,empty,made)==nf::Exist);
 assert(mapped.rename(mapped.root(),"made",mapped.root(),"moved")==nf::Ok);
 assert(mapped.remove(mapped.root(),"moved",false)==nf::Ok);
 assert(mapped.create(mapped.root(),"folder",true,1,0,empty,made)==nf::Ok);
 assert(mapped.remove(mapped.root(),"folder",true)==nf::Ok);
 nf::Space space;assert(mapped.space(space)==nf::Ok&&space.freeBytes+2==space.totalBytes);
 assert(!storage.reconcile());assert(!files.write("/local",0,text,4,false,n));assert(storage.reconcile());
 assert(!files.mkdir("/书籍"));assert(!files.write("/书籍/阅读记录.txt",0,text,4,false,n));storage.reconcile();uint64_t chineseDir,chineseFile;
 assert(mapped.lookup(mapped.root(),"书籍",chineseDir)==nf::Ok);assert(mapped.lookup(chineseDir,"阅读记录.txt",chineseFile)==nf::Ok);n=4;uint8_t readback[4];assert(mapped.read(chineseFile,0,readback,n)==nf::Ok&&!memcmp(readback,text,4));
 assert(mapped.rename(chineseDir,"阅读记录.txt",chineseDir,"阅读进度.txt")==nf::Ok);nf::Attr info;assert(mapped.getattr(chineseFile,info)==nf::Ok);
 Transport t;ResourceBudget budget;NfsService service(vfs,budget);NfsOptions options;options.wire.bytesPerSecond=0;uint32_t now=1;
 assert(!service.start(options,t,123));
 JsonDocument limits,observed;limits["memory_max_bytes"]=131072;limits["heap_reserve_bytes"]=32768;assert(!service.memory(limits.as<JsonObjectConst>(),observed.to<JsonObject>()));assert(observed["active"]["memory_max_bytes"]==131072);assert(service.active()&&vfs.pinned(files));
 assert(service.start(options,t,124));
 auto mountArgs=args([](nf::XdrOut&x){x.string("/sd");});
 auto result=transact(service,t,call(1,100005,1,mountArgs),now);auto x=reply(result,1);assert(x.u32()==nf::Ok);
 size_t len;const uint8_t*h=x.opaque(len,64);assert(x.valid&&h);std::vector<uint8_t>root(h,h+len);
 auto lookupArgs=args([&](nf::XdrOut&x){x.opaque(root.data(),root.size());x.string("hello");});
 result=transact(service,t,call(2,100003,3,lookupArgs),now);x=reply(result,2);assert(x.u32()==nf::Ok);h=x.opaque(len,64);assert(x.valid&&h);std::vector<uint8_t>file(h,h+len);
 auto writeArgs=args([&](nf::XdrOut&x){x.opaque(file.data(),file.size());x.u64(0);x.u32(4);x.u32(2);x.opaque(text,4);});
 result=transact(service,t,call(3,100003,7,writeArgs),now);x=reply(result,3);assert(x.u32()==nf::Ok);files.stat("/hello",fi);assert(fi.size==4);
 auto readArgs=args([&](nf::XdrOut&x){x.opaque(file.data(),file.size());x.u64(0);x.u32(4);});
 result=transact(service,t,call(4,100003,6,readArgs),now);x=reply(result,4);assert(x.u32()==nf::Ok);assert(x.boolean());x.fixed(84);assert(x.u32()==4&&x.boolean());const uint8_t*p=x.opaque(len,4096);assert(len==4&&!memcmp(p,text,4));
 assert(vfs.lockRead("/storage/user/hello",9));result=transact(service,t,call(5,100003,7,writeArgs),now);x=reply(result,5);assert(x.u32()==nf::Jukebox);vfs.release(9);
 assert(!files.write("/local",0,text,1,false,n));assert(service.poll(++now));
 result=transact(service,t,call(6,100003,6,readArgs),now);x=reply(result,6);assert(x.u32()==nf::Stale);
 service.stop();assert(!service.active()&&!vfs.pinned(files)&&!t.listening&&!t.alive);
 JsonDocument status;service.read(status.to<JsonObject>());assert(status["owned_heap_bytes"]==0);
 Transport fail;fail.failListen=true;assert(service.start(options,fail,99)&&!vfs.pinned(files));
 MemoryFiles ram("ram","/storage/ram","ram");vfs.add(ram);strcpy(options.provider,"ram");assert(service.start(options,t,999));
 options.wire.readOnly=true;assert(!service.start(options,t,1000));service.stop();
 // The public bridge accepts larger runtime payloads and transports UTF-8 names over NFS RPC.
 strcpy(options.provider,"user");options.wire.readOnly=false;options.wire.blockSize=16384;options.memory.maxBytes=131072;
 assert(files.budget(65536));assert(!files.write("/中文RPC.txt",0,text,4,false,n));assert(!service.start(options,t,2000));t.accepted=false;
 result=transact(service,t,call(70,100005,1,mountArgs),now);x=reply(result,70);assert(x.u32()==nf::Ok);h=x.opaque(len,64);root.assign(h,h+len);
 lookupArgs=args([&](nf::XdrOut&x){x.opaque(root.data(),root.size());x.string("中文RPC.txt");});result=transact(service,t,call(71,100003,3,lookupArgs),now);x=reply(result,71);assert(x.u32()==nf::Ok);h=x.opaque(len,64);file.assign(h,h+len);
 std::vector<uint8_t>large(16384);for(size_t i=0;i<large.size();++i)large[i]=uint8_t(i);
 writeArgs=args([&](nf::XdrOut&x){x.opaque(file.data(),file.size());x.u64(0);x.u32(large.size());x.u32(2);x.opaque(large.data(),large.size());});
 result=transact(service,t,call(72,100003,7,writeArgs),now);x=reply(result,72);assert(x.u32()==nf::Ok);
 readArgs=args([&](nf::XdrOut&x){x.opaque(file.data(),file.size());x.u64(0);x.u32(large.size());});result=transact(service,t,call(73,100003,6,readArgs),now);x=reply(result,73);assert(x.u32()==nf::Ok&&x.boolean());x.fixed(84);assert(x.u32()==large.size()&&x.boolean());p=x.opaque(len,32768);assert(len==large.size()&&!memcmp(p,large.data(),len));assert(!service.stop());assert(budget.promised()==0);
 std::cout<<"NFS framed RPC mount/read/write, OS leases, external invalidation, volatile semantics and lifecycle passed\n";
}
