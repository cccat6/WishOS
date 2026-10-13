#include "LuaRuntime.h"
#include "Workers.h"
#include "HttpApplications.h"
#include "SocketService.h"
#include "DatagramService.h"
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <vector>
#include <chrono>
using namespace notesos;
struct FaultHeap {
 size_t calls=0,failAt=SIZE_MAX;std::unordered_map<void*,size_t>blocks;
 static void*resize(void*ctx,void*p,size_t n){auto&s=*(FaultHeap*)ctx;if(!n){if(p){assert(s.blocks.erase(p)==1);free(p);}return nullptr;}
  auto old=s.blocks.find(p);if(p)assert(old!=s.blocks.end());
  // ArduinoJson and Lua require shrinking reallocations to succeed. Retain the block.
  if(p&&n<=old->second)return p;
  if(s.calls++>=s.failAt)return nullptr;auto out=realloc(p,n);if(!out)return nullptr;if(p)s.blocks.erase(old);s.blocks[out]=n;return out;}
};
struct JsonFaultHeap:ArduinoJson::Allocator {
 FaultHeap heap;
 void*allocate(size_t n)override{return FaultHeap::resize(&heap,nullptr,n);}
 void deallocate(void*p)override{FaultHeap::resize(&heap,p,0);}
 void*reallocate(void*p,size_t n)override{return FaultHeap::resize(&heap,p,n);}
};
static Core*core;static Workers*workers;static SocketService*tcp;static HttpApplications*http;static DatagramService*udp;
static std::atomic<bool>paused{false};static std::vector<std::thread>threads;static unsigned effects=0;
static JsonDocument parse(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static const char*effect(const char*,JsonObjectConst a,JsonObject o){++effects;o["echo"]=a;return nullptr;}
static const char*timer(const char*,JsonObjectConst a,JsonObject o){return core->timer(a,o);}
static unsigned port(){int fd=socket(AF_INET,SOCK_STREAM,0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(fd,(sockaddr*)&a,sizeof a));socklen_t n=sizeof a;getsockname(fd,(sockaddr*)&a,&n);close(fd);return ntohs(a.sin_port);}
static bool allow=true;static bool admission(size_t){return allow;}
int main(){
 Core c;core=&c;Vfs v;Workers w(c,v,{2,[](void(*fn)(void*),void*arg,unsigned){threads.emplace_back([=]{while(paused.load())std::this_thread::yield();fn(arg);});return true;},[](void*){std::this_thread::yield();},admission});workers=&w;SocketService sockets(c);tcp=&sockets;HttpApplications web(c);http=&web;DatagramService datagrams(c);udp=&datagrams;
 c.add({"/effect","write","",effect});c.add({"/sys/timers","write","",timer});
 c.add({"/sys/workers","submit,read,cancel,release","",[](const char*m,JsonObjectConst a,JsonObject o){return workers->invoke(m,a,o);}});
 c.add({"/net/tcp","listen,read,close","",[](const char*m,JsonObjectConst a,JsonObject o){return tcp->invoke(m,a,o);}});
 c.add({"/net/http-server","route,read,respond,stop","",[](const char*m,JsonObjectConst a,JsonObject o){return http->invoke(m,a,o);}});
 c.add({"/net/udp","open,read,close","",[](const char*m,JsonObjectConst a,JsonObject o){return udp->invoke(m,a,o);}});
 // Every VM allocator call is a failure boundary, from init through event conversion and stop.
 const char*script=R"(notes.subscribe('/effect')
 function on_start() notes.call('/sys/timers','write',{delay_ms=10});local s=string.rep('x',400);assert(notes.decode(notes.encode({x=s,n=notes.null})).x==s);notes.call('/effect','write',{x=s}) end
 function on_event(e) local s=notes.unbase64(notes.base64(e.data.value));notes.call('/effect','write',{value=s});return true end
 function on_stop() notes.call('/effect','write',{stop=true}) end)";
 Event event;copy(event.path,sizeof event.path,"/effect");copy(event.name,sizeof event.name,"changed");copy(event.data,sizeof event.data,"{\"value\":\"test\"}");
 size_t baseline=0;JsonDocument out,empty;
 for(size_t pass=0;pass==0||pass<=baseline+1;++pass){FaultHeap heap;if(pass)heap.failAt=pass-1;
  {LuaRuntime vm(c,65536,20000);vm.allocationBackend(FaultHeap::resize,&heap);bool loaded=vm.load(script,strlen(script));if(loaded&&vm.start()){vm.event(event);vm.stop();}JsonDocument state;vm.describe(state.to<JsonObject>());assert(state["memory_used"].as<size_t>()<=65536);}
  assert(heap.blocks.empty());c.listTimers(out["timers"].to<JsonArray>());assert(out["timers"].size()==0);if(!pass)baseline=heap.calls;
 }
 std::cout<<"Allocation failure sweep: "<<baseline+1<<" VM allocation boundaries, zero surviving allocations/timers\n";
 // JSON bridge allocation failure must not become a successful empty command or partial event.
 size_t jsonBaseline=0;
 for(size_t pass=0;pass==0||pass<=jsonBaseline+1;++pass){JsonFaultHeap heap;if(pass)heap.heap.failAt=pass-1;
  {LuaRuntime vm(c,65536,20000);vm.jsonAllocationBackend(&heap);if(vm.load(script,strlen(script))&&vm.start()){vm.event(event);vm.stop();}}
  assert(heap.heap.blocks.empty());c.listTimers(out["timers"].to<JsonArray>());assert(out["timers"].size()==0);if(!pass)jsonBaseline=heap.heap.calls;
 }
 std::cout<<"JSON bridge allocation failure sweep: "<<jsonBaseline+1<<" boundaries recovered\n";
 JsonFaultHeap rejected;rejected.heap.failAt=0;JsonDocument unavailable(&rejected);unsigned beforeEffects=effects;assert(!strcmp(c.invoke("/effect","write",empty.as<JsonObjectConst>(),unavailable["result"].to<JsonObject>()),"response_memory_low")&&effects==beforeEffects);
 // Compute conversion also runs under Lua protection, including allocation failure while serializing results.
 const char*compute="function compute(x) local t={} for i=1,48 do t[i]={n=i,value=string.rep('v',8)} end return t end";size_t computeBaseline=0;
 for(size_t pass=0;pass==0||pass<=computeBaseline+1;++pass){FaultHeap heap;if(pass)heap.failAt=pass-1;
  {LuaRuntime vm(c,65536,20000);vm.isolate(nullptr,nullptr,nullptr);vm.allocationBackend(FaultHeap::resize,&heap);if(vm.load(compute,strlen(compute))){JsonDocument input,output;vm.compute(input.as<JsonVariantConst>(),output.to<JsonVariant>());}}
  assert(heap.blocks.empty());if(!pass)computeBaseline=heap.calls;
 }
 std::cout<<"Compute allocation failure sweep: "<<computeBaseline+1<<" allocation boundaries recovered\n";
 // Deterministically hold two real threads while the main event loop/network remains live.
 auto owner=c.acquireOwner(),other=c.acquireOwner();paused=true;auto request=parse(R"({"source":"function compute(x) local s=0 for i=1,1000 do s=s+i end return {sum=s} end","input":{}})");
 allow=false;assert(!strcmp(c.invoke("/sys/workers","submit",request.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"worker_memory_budget"));allow=true;
 assert(!c.invoke("/sys/workers","submit",request.as<JsonObjectConst>(),out.to<JsonObject>(),owner));unsigned first=out["id"];
 assert(!c.invoke("/sys/workers","submit",request.as<JsonObjectConst>(),out.to<JsonObject>(),owner));
 assert(!strcmp(c.invoke("/sys/workers","submit",request.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"worker_capacity"));auto id=parse("{}");id["id"]=first;
 assert(!strcmp(c.invoke("/sys/workers","cancel",id.as<JsonObjectConst>(),out.to<JsonObject>(),other),"worker_owned_by_other"));
 assert(!strcmp(c.invoke("/sys/workers","release",id.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"worker_running"));
 auto route=parse(R"({"path":"/app/alive","body":"alive"})");assert(!c.invoke("/net/http-server","route",route.as<JsonObjectConst>(),out.to<JsonObject>(),owner));
 for(unsigned i=0;i<100;++i){c.clock(i);c.tick();w.poll();assert(web.handle("/app/alive","GET","",i,out.to<JsonObject>())&&out["body"]=="alive");}
 // Other scripts cannot overwrite/stop the owner's route; the administrative owner can inspect it.
 assert(!strcmp(c.invoke("/net/http-server","route",route.as<JsonObjectConst>(),out.to<JsonObject>(),other),"route_owned_by_other"));c.invoke("/net/http-server","stop",empty.as<JsonObjectConst>(),out.to<JsonObject>(),other);assert(web.active());
 // TCP accepted sockets inherit ownership, and owner release closes both sides without touching system sockets.
 auto listen=parse("{}");listen["address"]="127.0.0.1";listen["port"]=port();assert(!c.invoke("/net/tcp","listen",listen.as<JsonObjectConst>(),out.to<JsonObject>(),owner));
 int peer=socket(AF_INET,SOCK_STREAM,0);sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons(listen["port"].as<unsigned>());assert(!connect(peer,(sockaddr*)&address,sizeof address));sockets.poll();
 for(int retry=0;retry<200;++retry){sockets.poll();c.invoke("/net/tcp","read",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner);if(out["sockets"].size()==2)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(out["sockets"].size()==2);auto accepted=out["sockets"][1]["id"].as<unsigned>();id["id"]=accepted;
 assert(!strcmp(c.invoke("/net/tcp","close",id.as<JsonObjectConst>(),out.to<JsonObject>(),other),"socket_owned_by_other"));
 c.releaseOwner(owner);assert(!web.active()&&!sockets.active());close(peer);paused=false;for(auto&t:threads)t.join();threads.clear();w.poll();w.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["jobs"].size()==0&&!w.active());
 // A subsequent app still runs, proving failure did not leave the registry or task slots unusable.
 assert(!c.invoke("/sys/workers","submit",request.as<JsonObjectConst>(),out.to<JsonObject>(),other));id["id"]=out["id"];for(auto&t:threads)t.join();threads.clear();w.invoke("read",id.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["ok"]==true&&out["result"]["sum"]==500500);w.invoke("release",id.as<JsonObjectConst>(),out.to<JsonObject>());
 // UDP uses the same lifecycle: rejection from another owner and clean rebind after release.
 auto datagram=parse("{}");datagram["address"]="127.0.0.1";datagram["port"]=port();assert(!c.invoke("/net/udp","open",datagram.as<JsonObjectConst>(),out.to<JsonObject>(),other));
 assert(!strcmp(c.invoke("/net/udp","close",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"udp_owned_by_other"));c.releaseOwner(other);assert(!datagrams.active());assert(!c.invoke("/net/udp","open",datagram.as<JsonObjectConst>(),out.to<JsonObject>(),owner));c.releaseOwner(owner);
 // Real Lua fault triggers all native resource cleanup even without on_stop.
 paused=true;{
   LuaRuntime vm(c,65536);const char*source=R"(function on_start()
    assert(notes.call('/net/http-server','route',{path='/app/fault',body='temporary'}).ok)
    assert(notes.call('/sys/workers','submit',{source='function compute(x) while true do end end',input={}}).ok)
    notes.call('/sys/timers','write',{delay_ms=10});error('deliberate failure')
   end)";assert(vm.load(source,strlen(source)));assert(!vm.start());assert(!web.active());
 }
 paused=false;for(auto&t:threads)t.join();threads.clear();w.poll();w.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["jobs"].size()==0);
 // Sustained real execution, not just pending tasks: alternate two workers with HTTP and timer dispatch.
 unsigned polls=0;auto runningRoute=parse(R"({"path":"/app/progress","body":"ok"})");assert(!web.invoke("route",runningRoute.as<JsonObjectConst>(),out.to<JsonObject>()));
 auto calculation=parse(R"({"source":"function compute(x) local s=0 for i=1,20000 do s=s+i end return s end","instruction_budget":1000000})");
 for(unsigned round=0;round<12;++round){unsigned ids[2];for(unsigned n=0;n<2;++n){assert(!w.invoke("submit",calculation.as<JsonObjectConst>(),out.to<JsonObject>()));ids[n]=out["id"];}
   auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
   while(w.active()){assert(std::chrono::steady_clock::now()<deadline);++polls;c.clock(100+polls);c.tick();w.poll();assert(web.handle("/app/progress","GET","",100+polls,out.to<JsonObject>()));std::this_thread::yield();}
   for(auto&t:threads)t.join();threads.clear();for(auto task:ids){id["id"]=task;w.invoke("read",id.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["ok"]==true&&out["result"]==200010000);w.invoke("release",id.as<JsonObjectConst>(),out.to<JsonObject>());}
 }
 assert(polls>0);web.clear();std::cout<<"24 parallel calculations completed while main-loop HTTP/timers progressed for "<<polls<<" polls\n";
 std::cout<<"Two-worker concurrency, live HTTP dispatch, low-memory admission, ownership conflict, inherited sockets and orphan recovery passed\n";
}
