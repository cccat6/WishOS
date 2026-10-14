#include "ScriptHost.h"
#include "ButtonGestures.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
using namespace notesos;
static Core core;static ScriptHost*runtime;static std::map<std::string,unsigned>counts;
static ButtonGestures buttons({"A","B"},nullptr,[](void*,const char*event,JsonObjectConst d){core.emit("/dev/buttons",event,d);});
static uint32_t cursor=0;
static void sample(bool a,bool b,uint32_t time){core.clock(time);bool raw[]={a,b};buttons.sampleAll(raw,2,time);core.tick();core.dispatch(cursor);runtime->tick(time);}
int main(){Vfs vfs;MemoryFiles ram("ram","/storage/ram","ram");vfs.add(ram);ScriptHost app(core,vfs);runtime=&app;core.setEventObserver([](const Event&e){return runtime->event(e);});
 core.add({"/dev/buttons","read,configure","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure"))return buttons.configure(a,o);buttons.read(o);return nullptr;},nullptr,"read"});
 core.add({"/sys/timers","write,remove","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"write"))return core.timer(a,o);core.removeTimer(a["id"]|0u);return nullptr;}});
 core.add({"/effect","write","",[](const char*,JsonObjectConst a,JsonObject)->const char*{++counts[a["id"]|""];return nullptr;}});
 std::ifstream lib("hardware/agent-os/runtime/lib/input.lua");std::string code((std::istreambuf_iterator<char>(lib)),{});size_t written;assert(!ram.write("/input.lua",0,(const uint8_t*)code.data(),code.size(),true,written));
 const char*program=R"lua(
local input=notes.require('input.lua')
function on_start()
 assert(notes.call('/dev/buttons','configure',{defaults={debounce_ms=10}}).ok)
 input.watch('/dev/buttons',{
  {id='single',button='A',event='press',defer_ms=100},
  {id='together',event='press',chord={buttons={'A','B'},window_ms=75},cancel_pending={'single'}},
  {id='held-short',event='release',button='B',max_hold_ms=200,with={A={down=true,min_hold_ms=600}}},
  {id='sequence',steps={
   {event='release',button='A',max_hold_ms=200},
   {event='release',button='B',max_hold_ms=200,after_ms=40,within_ms=180,jitter_ms=15}
  }}
 },function(id,event) assert(notes.call('/effect','write',{id=id}).ok) end)
end)lua";
 assert(!ram.write("/main.lua",0,(const uint8_t*)program,strlen(program),true,written));JsonDocument a,out;a["file"]="/storage/ram/main.lua";a["memory_bytes"]=65536;
 assert(!app.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));
 sample(true,true,100);sample(true,true,117);sample(true,true,300);assert(counts["together"]==1&&counts["single"]==0);
 sample(false,false,550);sample(false,false,565);
 sample(true,false,1000);sample(true,false,1012);sample(true,true,1630);sample(true,true,1651);sample(true,false,1710);sample(true,false,1721);assert(counts["held-short"]==1);
 sample(false,false,1800);sample(false,false,1811);
 sample(true,false,2400);sample(true,false,2412);sample(false,false,2500);sample(false,false,2519);
 sample(false,true,2600);sample(false,true,2618);sample(false,false,2689);sample(false,false,2703);assert(counts["sequence"]==1);
 sample(true,false,3000);sample(true,false,3015);sample(false,false,3100);sample(false,false,3115);
 sample(false,true,3200);sample(false,true,3215);sample(false,false,3300);sample(false,false,3315);assert(counts["sequence"]==1);
 app.invoke("read",JsonObjectConst(),out.to<JsonObject>());if(out["fault"]==true){serializeJson(out,std::cerr);assert(false);}
 assert(!app.invoke("stop",JsonObjectConst(),out.to<JsonObject>()));assert(!core.nextTimer());assert(core.resources().promised()==0);
 std::cout<<"Runtime input patterns: simultaneous keys, deferred single cancellation, held/short combinations, sequence windows, jitter, input timestamps and owner cleanup passed\n";
}
