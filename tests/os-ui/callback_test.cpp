#include "ScriptHost.h"
#include "RemoteRequests.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <thread>
using namespace notesos;
static Core*kernel;static LuaRuntime*app;static unsigned invoked=0,effects=0;static uint32_t nextId=1;
static const char*effect(const char*,JsonObjectConst a,JsonObject){effects+=a["amount"]|1u;return nullptr;}
static const char*work(const char*,JsonObjectConst a,JsonObject o){if(a["reject"]|false)return "rejected";++invoked;auto id=nextId++;o["ticket"]=id;JsonDocument d;d["operation"]=id;d["value"]=a["value"]|1;kernel->emit("/result","ready",d.as<JsonObjectConst>(),kernel->callerOwner());return nullptr;}
static void contract(JsonObject o){asyncContract(o,"begin","/result",{"ready","failed"})["operation"]="ticket";}
static void stats(LuaRuntime&vm,JsonDocument&out){out.clear();vm.describe(out.to<JsonObject>());}
struct Heap {
 size_t calls=0,failAt=SIZE_MAX;std::unordered_map<void*,size_t>blocks;
 static void*resize(void*ctx,void*p,size_t n){auto&s=*static_cast<Heap*>(ctx);if(!n){if(p){assert(s.blocks.erase(p));free(p);}return nullptr;}auto old=s.blocks.find(p);if(p){assert(old!=s.blocks.end());if(n<=old->second)return p;}if(s.calls++>=s.failAt)return nullptr;auto result=realloc(p,n);if(!result)return nullptr;if(p)s.blocks.erase(old);s.blocks[result]=n;return result;}
};
static void send(Core&c,uint32_t&cursor,const char*source,const char*name,const char*data="{}",uint32_t owner=0){JsonDocument d;assert(!deserializeJson(d,data));assert(c.emit(source,name,d.as<JsonObjectConst>(),owner));c.dispatch(cursor);}
int main(){
 Core c;kernel=&c;c.add({"/effect","write","",effect});c.add({"/work","begin","",work,contract});
 c.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return kernel->describe(a["path"]|"",o);}});
 c.setEventObserver([](const Event&e){return app?app->event(e):false;});JsonDocument out;
 const char*events=R"lua(
local b,new,once,legacy=0,false,0,0
function on_start()
 notes.subscribe('/button')
 notes.on('/button','click',function(e)
  assert(e.data.button=='A');notes.off(b)
  if not new then new=true;notes.on('/button','click',function() notes.call('/effect','write',{amount=10}) end) end
  notes.call('/effect','write',{amount=1});return true
 end,{where={button='A'}})
 b=notes.on('/button','click',function() error('removed listener ran') end)
 notes.on('/button','click',function() once=once+1;assert(once==1);notes.call('/effect','write',{amount=100}) end,{once=true,where={button='A'}})
end
function on_event(e) legacy=legacy+1;notes.call('/effect','write',{amount=1000}) end
)lua";
 {LuaRuntime vm(c,65536);app=&vm;assert(vm.load(events,strlen(events))&&vm.start());uint32_t cursor=c.sequence();
  send(c,cursor,"/button","click",R"({"button":"A"})");assert(effects==101); // mutation snapshot; independent one-shot still receives consumed event
  send(c,cursor,"/button","click",R"({"button":"A"})");assert(effects==112);
  send(c,cursor,"/button","release",R"({"button":"A"})");assert(effects==1112); // legacy subscription remains
  vm.stop();stats(vm,out);assert(out["callbacks"]==0);send(c,cursor,"/button","click",R"({"button":"A"})");assert(effects==1112);
 }app=nullptr;
 const char*async=R"lua(
local returned=false
function on_start()
 local rejected,token=notes.request('/work','begin',{reject=true},function() error('rejection callback') end)
 assert(not rejected.ok and token==nil)
 local unsupported=notes.request('/effect','write',{},function() end);assert(not unsupported.ok)
 local first,h=notes.request('/work','begin',{value=2},function(e) assert(returned and e.data.value==2);notes.call('/effect','write',{amount=2}) end);assert(first.ok and h)
 local second,k=notes.request('/work','begin',{value=3},function(e) assert(returned and e.data.value==3);notes.call('/effect','write',{amount=3}) end);assert(second.ok and k)
 local third,gone=notes.request('/work','begin',{},function() error('cancelled callback') end);assert(third.ok and notes.off(gone));returned=true
end
)lua";
 unsigned before=effects,operations=invoked;
 {LuaRuntime vm(c,65536);app=&vm;uint32_t cursor=c.sequence();assert(vm.load(async,strlen(async))&&vm.start());assert(effects==before&&invoked==operations+3);c.dispatch(cursor);assert(effects==before+5&&!vm.error()[0]);stats(vm,out);assert(out["callbacks"]==0);send(c,cursor,"/result","ready",R"({"operation":1,"value":2})");assert(effects==before+5);}
 app=nullptr;
 // Manual unsubscribe does not disconnect registered callbacks, and listeners
 // registered after an event was queued do not receive the old event.
 const char*ownership=R"lua(function on_start()
 notes.subscribe('/x');local h=notes.on('/x','changed',function() notes.call('/effect','write',{}) end)
 notes.unsubscribe('/x');local tmp=notes.on('/x','ignored',function()end);assert(notes.off(tmp))
 end function on_event() error('unsubscribed legacy handler') end)lua";
 {LuaRuntime vm(c,49152);app=&vm;uint32_t cursor=c.sequence();JsonDocument d;assert(c.emit("/x","changed",d.to<JsonObject>()));before=effects;assert(vm.load(ownership,strlen(ownership))&&vm.start());c.dispatch(cursor);assert(effects==before);send(c,cursor,"/x","changed");assert(effects==before+1&&!vm.error()[0]);stats(vm,out);auto owner=out["owner"].as<uint32_t>();send(c,cursor,"/x","changed","{}",owner+1);assert(effects==before+1);}
 app=nullptr;
 // Raw subscriptions and managed listeners share the same future-event
 // boundary. Repeating subscribe is idempotent and does not move that boundary.
 {LuaRuntime vm(c,49152);app=&vm;uint32_t cursor=c.sequence();JsonDocument d;assert(c.emit("/raw","changed",d.to<JsonObject>()));before=effects;
  const char*s="function on_start() notes.subscribe('/raw') end function on_event(e) notes.subscribe('/raw');notes.call('/effect','write',{}) end";
  assert(vm.load(s,strlen(s))&&vm.start());c.dispatch(cursor);assert(effects==before);
  assert(c.emit("/raw","changed",d.to<JsonObject>()));assert(c.emit("/raw","changed",d.to<JsonObject>()));c.dispatch(cursor);assert(effects==before+2);
 }app=nullptr;
 // Idempotent remote submit may already have completed. Its callback is deferred
 // to a later runtime pump, without re-executing the operation or missing receipt.
 {Core local;kernel=&local;Vfs files;RemoteRequests remote(local);ScriptHost runtime(local,files);
  local.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return kernel->describe(a["path"]|"",o);}});
  local.add({"/effect","write","",effect});
  // Use a small completed-result adapter with the same declarative contract.
  local.add({"/cached","submit","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["id"]=77;o["state"]="completed";o["ok"]=true;return nullptr;},[](JsonObject o){auto match=asyncContract(o,"submit","/cached");match["id"]="id";o["async"]["submit"]["settled"]["field"]="state";o["async"]["submit"]["settled"]["events"]["completed"]="completed";}});
  JsonDocument a;a["source"]="local returned=false function on_start() local r=notes.request('/cached','submit',{},function(e) assert(returned and e.data.id==77);notes.call('/effect','write',{amount=7}) end);assert(r.ok);returned=true end";a["memory_bytes"]=65536;before=effects;assert(!runtime.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(effects==before);runtime.tick(1);assert(effects==before+7);runtime.tick(2);assert(effects==before+7);
 }kernel=&c;
 // A completed request survives an overflowing ordinary event journal.
 {LuaRuntime vm(c,65536,200000);app=&vm;const char*s="function on_start() assert(notes.request('/work','begin',{value=9},function(e) assert(e.data.value==9);notes.call('/effect','write',{amount=9}) end).ok);for i=1,48 do print('noise') end end";
  uint32_t cursor=c.sequence();before=effects;assert(vm.load(s,strlen(s))&&vm.start());c.dispatch(cursor);assert(effects==before);assert(vm.flushCallbacks());assert(effects==before+9);stats(vm,out);assert(out["callbacks"]==0);vm.flushCallbacks();assert(effects==before+9);
 }app=nullptr;
 // A native producer can retain the receipt even if its ordinary RTOS queue is
 // full and no Core journal event is delivered at all.
 c.add({"/native","begin","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["ticket"]=111;return nullptr;},contract});
 {LuaRuntime vm(c,65536);app=&vm;const char*s="function on_start() assert(notes.request('/native','begin',{},function(e) assert(e.data.operation==111);notes.call('/effect','write',{}) end).ok) end";
  assert(vm.load(s,strlen(s))&&vm.start());stats(vm,out);auto owner=out["owner"].as<uint32_t>();JsonDocument event;event["operation"]=111;
  before=effects;c.completions().record("/result","ready",event.as<JsonObjectConst>(),owner,123);assert(vm.flushCallbacks());assert(effects==before+1);
 }app=nullptr;
 // A producer thread races with journal readers/dispatch. The registered
 // source uses the same retained completion path as the target FreeRTOS tasks.
 c.add({"/result","read","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;}});
 {LuaRuntime vm(c,65536);app=&vm;const char*s="function on_start() assert(notes.request('/native','begin',{},function(e) assert(e.data.operation==111);notes.call('/effect','write',{}) end).ok) end";
  assert(vm.load(s,strlen(s))&&vm.start());stats(vm,out);auto owner=out["owner"].as<uint32_t>();before=effects;uint32_t cursor=c.sequence();
  std::thread producer([&]{JsonDocument event;event["operation"]=111;c.emit("/result","ready",event.as<JsonObjectConst>(),owner);for(unsigned i=0;i<1000;++i)c.emit("/noise","changed",event.as<JsonObjectConst>());});
  for(unsigned i=0;i<100;++i){JsonDocument journal;c.events(cursor,8,journal.to<JsonObject>());c.dispatch(cursor);vm.flushCallbacks();}
  producer.join();c.dispatch(cursor);vm.flushCallbacks();assert(effects==before+1);stats(vm,out);assert(out["callbacks"]==0);
 }app=nullptr;
 // Reject before invoking when there is no room to retain its result.
 {JsonDocument args,result,config;config["memory_limit_bytes"]=1;assert(!c.resources().configure(config.as<JsonObjectConst>()));operations=invoked;
  assert(!strcmp(c.request("/work","begin",args.to<JsonObject>(),result.to<JsonObject>(),123),"completion_memory_budget"));assert(invoked==operations);
  config["memory_limit_bytes"]=nullptr;assert(!c.resources().configure(config.as<JsonObjectConst>()));assert(c.resources().promised()==0);
 }
 // Source-subscription exhaustion is detected before an async operation begins.
 {LuaRuntime vm(c,131072);app=&vm;const char*s="function on_start() for i=1,16 do notes.on('/s/'..i,'changed',function()end) end local ok=pcall(function() notes.request('/work','begin',{},function()end) end);assert(not ok) end";operations=invoked;assert(vm.load(s,strlen(s))&&vm.start());assert(invoked==operations);}
 app=nullptr;
 // Callback count grows within the application budget, not a fixed 32-slot pool.
 {LuaRuntime vm(c,262144,200000);app=&vm;const char*s="function on_start() for i=1,96 do notes.on('/x','changed',function()end) end end";assert(vm.load(s,strlen(s))&&vm.start());stats(vm,out);assert(out["callbacks"]==96);uint32_t cursor=c.sequence();send(c,cursor,"/x","changed");assert(!vm.error()[0]);}app=nullptr;
 // Callback code obeys the same instruction and fault boundary as on_event.
 {LuaRuntime vm(c,65536,5000);app=&vm;const char*s="function on_start() notes.on('/x','changed',function() while true do end end) end";assert(vm.load(s,strlen(s))&&vm.start());uint32_t cursor=c.sequence();send(c,cursor,"/x","changed");assert(strstr(vm.error(),"instruction"));stats(vm,out);assert(out["callbacks"]==0);}
 app=nullptr;
 // Inject allocation failure across lazy loading, registration, reply binding,
 // callback dispatch and cleanup. No handler/native owner allocation survives.
 size_t baseline=0;for(size_t pass=0;pass==0||pass<=baseline;++pass){Heap heap;if(pass)heap.failAt=pass-1;
  {LuaRuntime vm(c,65536);app=&vm;vm.allocationBackend(Heap::resize,&heap);uint32_t cursor=c.sequence();if(vm.load(async,strlen(async))&&vm.start())c.dispatch(cursor);vm.stop();app=nullptr;}
  assert(heap.blocks.empty()&&c.resources().promised()==0);if(!pass)baseline=heap.calls;
 }
 std::cout<<"Callbacks: subscriptions, snapshots, reentrancy, one-shot request correlation, cancellation, replay, quotas and "<<baseline<<" allocation boundaries passed\n";
}
