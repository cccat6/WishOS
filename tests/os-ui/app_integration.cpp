#include "ScriptHost.h"
#include "SystemFiles.h"
#include "Workers.h"
#include "HttpApplications.h"
#include "RemoteRequests.h"
#include "Conditions.h"
#include "Policy.h"
#include <cassert>
#include <thread>
#include <vector>
#include <chrono>
#include <iostream>
using namespace notesos;
static Core*kernel;static ScriptHost*host;static Workers*pool;static HttpApplications*web;static RemoteRequests*remote;
static Conditions*watchers;static Policies*configuration;
static unsigned effects=0;static std::vector<std::thread>threads;
static JsonDocument parse(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static const char*timers(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"write"))return kernel->timer(a,o);if(!strcmp(m,"remove"))return kernel->removeTimer(a["id"]|0u)?nullptr:"not_found";kernel->listTimers(o["timers"].to<JsonArray>());return nullptr;}
static const char*effect(const char*,JsonObjectConst a,JsonObject o){++effects;o["echo"]=a;return nullptr;}
static void put(MemoryFiles&f,const char*path,const char*text){size_t n;assert(!f.write(path,0,(const uint8_t*)text,strlen(text),true,n));}
static bool failPersist(const char*,size_t){return false;}
int main(){
 Core c;kernel=&c;Vfs v;MemoryFiles ram("ram","/storage/ram","ram");SystemFiles system;v.add(ram);v.add(system);ScriptHost h(c,v);host=&h;
 Workers workers(c,v,{2,[](void(*fn)(void*),void*p,unsigned core){assert(core<2);threads.emplace_back([=]{fn(p);});return true;},[](void*){std::this_thread::yield();},nullptr});pool=&workers;
 HttpApplications apps(c);web=&apps;RemoteRequests requests(c);remote=&requests;
 Conditions conditions(c);watchers=&conditions;
 c.add({"/sys/runtime","read,load,stop,configure", "",[](const char*m,JsonObjectConst a,JsonObject o){return host->invoke(m,a,o);}});
 c.add({"/sys/workers","read,submit,cancel,release","",[](const char*m,JsonObjectConst a,JsonObject o){return pool->invoke(m,a,o);}});
 c.add({"/net/http-server","read,route,respond,release","",[](const char*m,JsonObjectConst a,JsonObject o){return web->invoke(m,a,o);}});
 c.add({"/services/remote","read,peek,submit,complete,release","",[](const char*m,JsonObjectConst a,JsonObject o){return remote->invoke(m,a,o,kernel->now(),"test");}});
 c.add({"/sys/capabilities","list,describe","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"describe"))return kernel->describe(a["path"]|"",o);kernel->list(o["devices"].to<JsonArray>());return nullptr;},nullptr,"list,describe"});
 c.add({"/sys/conditions","read,write,remove","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return watchers->set(a,o);if(!strcmp(m,"remove"))return watchers->remove(a["id"]|"");watchers->list(o);return nullptr;}});
 c.add({"/sensor","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["value"]=42;return nullptr;},[](JsonObject o){o["properties"]["value"]["type"]="number";o["properties"]["value"]["access"]="r";}});
 c.add({"/sys/timers","write,read,remove","",timers});c.add({"/effect","write","",effect});
 c.setEventObserver([](const Event&e){return host->event(e);});
 assert(!ram.mkdir("/app"));assert(!ram.mkdir("/app/lib"));
 put(ram,"/app/lib/value.lua","return {amount=7}");
 put(ram,"/app/lib/helper.lua","local v=notes.require('value.lua'); return {value=function() return v.amount end}");
 put(ram,"/app/main.lua",R"lua(
local helper=notes.require('lib/helper.lua')
assert(helper==notes.require('lib/helper.lua'))
assert(notes.resolve('ui/home.json')=='/storage/ram/app/ui/home.json')
assert(notes.encode(notes.array({}))=='[]')
assert(notes.decode('{"a":null}').a==notes.null)
assert(notes.encode(notes.decode('[]'))=='[]')
local bytes='\0\255hello\0';assert(notes.unhex(notes.hex(bytes))==bytes);assert(notes.unbase64(notes.base64(bytes))==bytes)
assert(notes.decode(notes.encode({bytes=bytes})).bytes==bytes)
assert(string.gmatch==nil and string.gsub==nil)
function on_start()
 local r=notes.call('/effect','write',{clear=notes.null,items=notes.array({})});assert(r.ok and r.result.echo.clear==notes.null and #r.result.echo.items==0)
 notes.subscribe('/sys/timers');local t=notes.call('/sys/timers','write',{delay_ms=10,period_ms=10});assert(t.ok);timer=t.result.id
end
function on_event(e) if e.source=='/sys/timers' and e.data.id==timer then notes.call('/effect','write',{value=helper.value()});return true end end
function on_stop() notes.call('/effect','write',{stopped=true}) end
)lua");
 JsonDocument out;auto a=parse(R"({"file":"/storage/ram/app/main.lua","memory_bytes":49152})");auto e=h.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>());if(e){std::cerr<<e<<":"<<(out["detail"]|"")<<":"<<(out["error"]|"")<<"\n";}assert(!e&&effects==1);
 uint32_t cursor=c.sequence();c.clock(10);c.tick();c.dispatch(cursor);assert(effects==2);
 // Failed replacement preparation preserves the live app and its owned timer.
 auto bad=parse(R"({"source":"this is not lua"})");assert(h.invoke("load",bad.as<JsonObjectConst>(),out.to<JsonObject>()));c.clock(20);c.tick();c.dispatch(cursor);assert(effects==3);
 JsonDocument empty;assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(effects==4);c.clock(30);c.tick();c.dispatch(cursor);assert(effects==4);c.listTimers(out["timers"].to<JsonArray>());assert(out["timers"].size()==0);
 // Replacing one app cannot accidentally retain or collide with the previous timer IDs.
 assert(!h.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(!h.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));c.listTimers(out["timers"].to<JsonArray>());assert(out["timers"].size()==1);
 assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));
 // Startup validation and persistence are atomic; RAM cannot be a reboot dependency.
 auto startup=parse(R"({"startup":{"auto_start":true,"file":"/storage/ram/app/main.lua"}})");assert(h.invoke("configure",startup.as<JsonObjectConst>(),out.to<JsonObject>()));
 startup=parse(R"({"startup":{"auto_start":false,"file":"/system/main.lua"},"persist":true})");assert(h.invoke("configure",startup.as<JsonObjectConst>(),out.to<JsonObject>(),failPersist));h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["configuration_revision"]==1);
 // Dynamic HTTP route -> event -> Lua read/response, without invoking a remote Agent.
 auto httpScript=parse(R"({"source":"function on_start() notes.subscribe('/net/http-server'); assert(notes.call('/net/http-server','route',{path='/app/test',dynamic=true}).ok) end function on_event(e) if e.source=='/net/http-server' then local r=notes.call('/net/http-server','read',{id=e.data.id}); assert(r.ok); notes.call('/net/http-server','respond',{id=e.data.id,body=r.result.body..'!'}); return true end end"})");
 assert(!h.invoke("load",httpScript.as<JsonObjectConst>(),out.to<JsonObject>()));cursor=c.sequence();assert(apps.handle("/app/test","GET","hello",31,out.to<JsonObject>()));c.dispatch(cursor);auto q=parse(R"({"id":1})");assert(!apps.invoke("read",q.as<JsonObjectConst>(),out.to<JsonObject>()));assert(out["body"]=="hello!"&&out["done"]==true);
 // A running compute worker cannot call a driver; normal main-loop timers continue.
 auto work=parse(R"({"source":"function compute(x) local sum=0 for i=1,x.n do sum=sum+i end return {sum=sum,clear=notes.null} end","input":{"n":10000},"core":1,"instruction_budget":1000000})");
 assert(!workers.invoke("submit",work.as<JsonObjectConst>(),out.to<JsonObject>()));auto id=out["id"].as<unsigned>();assert(out["core"]==1);
 auto timer=parse(R"({"id":123,"delay_ms":1,"period_ms":10})");assert(!c.timer(timer.as<JsonObjectConst>(),out.to<JsonObject>()));for(unsigned n=0;n<100;++n){c.clock(32+n);c.tick();workers.poll();std::this_thread::yield();}for(auto&t:threads)t.join();threads.clear();workers.poll();q["id"]=id;assert(!workers.invoke("read",q.as<JsonObjectConst>(),out.to<JsonObject>()));assert(out["ok"]==true&&out["result"]["sum"]==50005000);assert(!workers.invoke("release",q.as<JsonObjectConst>(),out.to<JsonObject>()));
 unsigned before=effects;work=parse(R"({"source":"function compute(x) return notes.call('/effect','write',{}) end","input":{}})");assert(!workers.invoke("submit",work.as<JsonObjectConst>(),out.to<JsonObject>()));q["id"]=out["id"];for(auto&t:threads)t.join();threads.clear();assert(!workers.invoke("read",q.as<JsonObjectConst>(),out.to<JsonObject>()));assert(out["ok"]==false&&strstr(out["error"].as<const char*>(),"worker_system_calls"));assert(effects==before);workers.invoke("release",q.as<JsonObjectConst>(),out.to<JsonObject>());
 work=parse(R"({"source":"function compute(x) while true do end end","instruction_budget":3000})");assert(!workers.invoke("submit",work.as<JsonObjectConst>(),out.to<JsonObject>()));q["id"]=out["id"];for(auto&t:threads)t.join();threads.clear();workers.invoke("read",q.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["ok"]==false);workers.invoke("release",q.as<JsonObjectConst>(),out.to<JsonObject>());
 // Cancellation is cooperative; unavailable affinity never consumes a slot.
 work=parse(R"({"source":"function compute(x) while true do end end","core":2})");assert(workers.invoke("submit",work.as<JsonObjectConst>(),out.to<JsonObject>()));
 work["core"]=0;work["instruction_budget"]=10000000;assert(!workers.invoke("submit",work.as<JsonObjectConst>(),out.to<JsonObject>()));q["id"]=out["id"];workers.invoke("cancel",q.as<JsonObjectConst>(),out.to<JsonObject>());for(auto&t:threads)t.join();threads.clear();workers.invoke("read",q.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["ok"]==false&&out["cancel_requested"]==true);workers.invoke("release",q.as<JsonObjectConst>(),out.to<JsonObject>());
 // A fault after timer creation releases the timer rather than leaving an orphaned trigger.
 auto fault=parse(R"({"source":"function on_start() notes.call('/sys/timers','write',{delay_ms=10}); error('stop') end"})");assert(h.invoke("load",fault.as<JsonObjectConst>(),out.to<JsonObject>()));c.listTimers(out["timers"].to<JsonArray>());assert(out["timers"].size()==1&&out["timers"][0]["id"]==123);
 assert(!h.restore(R"({"auto_start":true,"file":"/storage/ram/app/main.lua","memory_bytes":32768,"instruction_budget":20000,"delay_ms":0})"));
 // Missing startup code is a reported boot failure, not a failure of the system event loop.
 assert(h.restore(R"({"auto_start":true,"file":"/system/missing.lua","memory_bytes":32768,"instruction_budget":20000,"delay_ms":0})"));h.scheduleBoot(200);h.tick(200);h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["boot_pending"]==false&&out["boot_error"]=="not_found");
 // An online next-boot edit does not silently retarget a previously scheduled boot snapshot.
 assert(h.restore(R"({"auto_start":true,"file":"/system/default-app.json","memory_bytes":32768,"instruction_budget":20000,"delay_ms":10})"));h.scheduleBoot(300);
 auto nextBoot=parse(R"({"startup":{"file":"/system/missing.lua"}})");assert(!h.invoke("configure",nextBoot.as<JsonObjectConst>(),out.to<JsonObject>()));h.tick(310);h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["boot_error"]=="script_load_failed");
 // VM quota is a system resource policy, not a 64/128 KiB hardware boundary.
 auto large=parse(R"({"source":"function on_start() local text=string.rep('x',70000); assert(#text==70000) end","memory_bytes":196608})");if(auto error=h.invoke("load",large.as<JsonObjectConst>(),out.to<JsonObject>())){std::cerr<<error<<" ";serializeJsonPretty(out,std::cerr);assert(false);}assert(out["memory_peak"].as<unsigned>()>65536);assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));
 auto memoryPolicy=parse(R"({"default_vm_memory_bytes":49152})");assert(!c.resources().configure(memoryPolicy.as<JsonObjectConst>()));auto inherited=parse(R"({"source":"function on_start() end"})");assert(!h.invoke("load",inherited.as<JsonObjectConst>(),out.to<JsonObject>()));assert(out["memory_budget"]==49152);assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));
 c.add({"/custom/readout","sample","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["value"]=42;return nullptr;},nullptr,"sample"});
 auto custom=parse(R"({"source":"function on_prepare() local r=notes.call('/custom/readout','sample',{});assert(r.ok and r.result.value==42) end"})");assert(!h.invoke("load",custom.as<JsonObjectConst>(),out.to<JsonObject>()));
 // Failed candidate diagnostics remain valid after the candidate is destroyed.
 auto invalidCandidate=parse(R"({"source":"function on_prepare() error('candidate rejected') end"})");
 assert(h.invoke("load",invalidCandidate.as<JsonObjectConst>(),out.to<JsonObject>()));std::string ownedDiagnostics;serializeJson(out,ownedDiagnostics);assert(ownedDiagnostics.find("candidate rejected")!=std::string::npos);
 // Candidate preparation cannot mutate devices or stop the previous app.
 auto stable=parse(R"({"source":"function on_start() notes.subscribe('/button') end function on_event() return true end"})");assert(!h.invoke("load",stable.as<JsonObjectConst>(),out.to<JsonObject>()));unsigned snapshot=effects;
 auto unsafe=parse(R"({"source":"function on_prepare() notes.call('/effect','write',{}) end"})");assert(h.invoke("load",unsafe.as<JsonObjectConst>(),out.to<JsonObject>()));assert(effects==snapshot);h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["fault"]==false);
 // Explicit next-boot preparation happens before the source lookup. Malformed
 // preparation and unavailable operations preserve/report state instead of hanging.
 assert(!h.restore(R"({"auto_start":true,"file":"/system/main.lua","memory_bytes":65536,"instruction_budget":20000,"delay_ms":0,"prepare":[{"path":"/effect","method":"write"}]})"));
 assert(h.restore(R"({"auto_start":true,"file":"/system/missing.lua","memory_bytes":32768,"instruction_budget":20000,"delay_ms":0,"prepare":[{"path":"/effect","method":"write","args":{}}]})"));h.scheduleBoot(500);h.tick(500);assert(effects==snapshot+1);h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["boot_error"]=="not_found"&&out["fault"]==false);
 // Application teardown reclaims pending remote request slots as well as timers.
 auto pending=parse(R"({"source":"function on_start() assert(notes.call('/services/remote','submit',{route='service',payload={}}).ok) end"})");assert(!h.invoke("load",pending.as<JsonObjectConst>(),out.to<JsonObject>()));assert(requests.pending());assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(!requests.pending());
 // Early manual load supersedes a scheduled factory boot. Discovery works in
 // on_prepare, and a successful recovery clears the previous boot error.
 assert(h.restore(R"({"auto_start":true,"file":"/system/missing.lua","memory_bytes":null,"instruction_budget":20000,"delay_ms":10})"));h.scheduleBoot(600);
 auto manual=parse(R"({"source":"function on_prepare() local r=notes.call('/sys/capabilities','describe',{path='/sensor'}); assert(r.ok and r.result.properties.value.type=='number') end function on_start() end"})");
 assert(!h.invoke("load",manual.as<JsonObjectConst>(),out.to<JsonObject>()));h.tick(1000);h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["fault"]==false&&out["boot_pending"]==false&&out["boot_error"]=="");
 // App-owned conditions are reclaimed like timers; pending completions from an
 // old app cannot drive the replacement's handlers.
 auto watched=parse(R"({"source":"function on_start() assert(notes.call('/sys/conditions','write',{id='watch',source='/sensor',property='value',operator='above',value=10,emit_initial=true}).ok); assert(notes.call('/services/remote','submit',{request_id='owned',route='service',payload={}}).ok) end"})");
 assert(!h.invoke("load",watched.as<JsonObjectConst>(),out.to<JsonObject>()));uint32_t oldOwner=out["owner"];conditions.tick(0);conditions.list(out.to<JsonObject>());assert(out["conditions"].size()==1&&out["conditions"][0]["owner"]==oldOwner);
 uint32_t stranger=c.acquireOwner();auto watch=parse(R"({"id":"watch","source":"/sensor","property":"value","operator":"above","value":1})");
 assert(!strcmp(c.invoke("/sys/conditions","write",watch.as<JsonObjectConst>(),out.to<JsonObject>(),stranger),"condition_owned_by_other"));assert(!strcmp(c.invoke("/sys/conditions","remove",watch.as<JsonObjectConst>(),out.to<JsonObject>(),stranger),"condition_owned_by_other"));
 auto collision=parse(R"({"request_id":"owned","route":"service","payload":{}})");assert(!strcmp(c.invoke("/services/remote","submit",collision.as<JsonObjectConst>(),out.to<JsonObject>(),stranger),"request_owned_by_other"));
 assert(!c.invoke("/services/remote","peek",empty.as<JsonObjectConst>(),out.to<JsonObject>()));auto ownedId=out["id"].as<uint32_t>();auto reply=parse(R"({"ok":true,"result":{}})");reply["id"]=ownedId;
 assert(!strcmp(c.invoke("/services/remote","complete",reply.as<JsonObjectConst>(),out.to<JsonObject>(),stranger),"request_owned_by_other"));assert(requests.pending());
 auto receiver=parse(R"({"source":"function on_start() notes.subscribe('/sys/conditions') end function on_event() notes.call('/effect','write',{}) end"})");assert(!h.invoke("load",receiver.as<JsonObjectConst>(),out.to<JsonObject>()));conditions.list(out.to<JsonObject>());assert(out["conditions"].size()==0&&!requests.pending());
 unsigned priorEffects=effects;Event stale;stale.targetOwner=oldOwner;strcpy(stale.path,"/sys/conditions");strcpy(stale.name,"entered");snprintf(stale.data,sizeof stale.data,"{\"id\":\"watch\",\"owner\":%lu}",(unsigned long)oldOwner);h.event(stale);assert(effects==priorEffects);
 assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(c.resources().promised()==0);
 // A configured preparation call can stop the current runtime. Its cleanup must
 // not invalidate the still-executing boot snapshot or skip following steps.
 assert(h.restore(R"({"auto_start":true,"file":"/system/missing.lua","memory_bytes":null,"instruction_budget":20000,"delay_ms":0,"prepare":[{"path":"/sys/runtime","method":"stop","args":{}},{"path":"/effect","method":"write","args":{}}]})"));h.scheduleBoot(2000);priorEffects=effects;h.tick(2000);assert(effects==priorEffects+1);h.invoke("read",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(out["boot_error"]=="not_found");
 // Driver-specific negative and fractional values traverse the same runtime
 // call API, rather than depending on native-only configuration setters.
 const Setting fields[]={{"/custom/sensor","offset","integer","0","",-90,90},{"/custom/sensor","threshold","number","0.5","",0,1}};
 Policies configured(fields,2);configuration=&configured;
 c.add({"/sys/policy","read,write","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"read")){configuration->read("system",o);return nullptr;}return configuration->apply(a,o);},nullptr,"read"});
 auto configuredApp=parse(R"({"source":"function on_start() local r=notes.call('/sys/policy','write',{changes={{resource='/custom/sensor',settings={offset=-15,threshold=0.125}}}});assert(r.ok,r.error); local current=notes.call('/sys/policy','read',{}); assert(current.ok and current.result.effective['/custom/sensor'].offset==-15 and current.result.effective['/custom/sensor'].threshold==0.125) end"})");
 assert(!h.invoke("load",configuredApp.as<JsonObjectConst>(),out.to<JsonObject>()));assert(!h.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(c.resources().promised()==0);
 std::cout<<"Runtime modules, JSON/binary fidelity, owned timers, startup validation, HTTP event applications and isolated workers passed\n";
}
