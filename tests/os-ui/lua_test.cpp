#ifdef NDEBUG
#error Assertions must be enabled
#endif
#include "LuaRuntime.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static int writes=0;
static const char*sensor(const char*,JsonObjectConst,JsonObject o){o["value"]=42;return nullptr;}
static const char*effect(const char*,JsonObjectConst a,JsonObject){writes+=a["value"].as<int>();return nullptr;}
int main(){Core c;c.add({"/sensor","read","",sensor});c.add({"/effect","write","",effect});
 const char*source=R"(assert(io==nil and os==nil and package==nil and debug==nil and loadfile==nil and setmetatable==nil)
 assert(notes.decode(notes.encode({label="runtime",color="#00ff00"})).label=="runtime")
 notes.subscribe('/sensor')
 function on_start() local r=notes.call('/sensor','read',{}); assert(r.ok and r.result.value==42); notes.call('/effect','write',{value=r.result.value}) end
 function on_event(e) if e.event=='changed' then notes.call('/effect','write',{value=e.data.value}); return true end end)";
 {LuaRuntime vm(c,49152,10000);assert(vm.load(source,strlen(source)));assert(writes==0);assert(vm.start());assert(writes==42);Event e;e.sequence=c.sequence()+1;copy(e.path,sizeof(e.path),"/sensor");copy(e.name,sizeof(e.name),"changed");copy(e.data,sizeof(e.data),"{\"value\":7}");assert(vm.event(e));assert(writes==49);}
 {LuaRuntime vm(c);const char*s="notes.call('/effect','write',{value=100})";assert(!vm.load(s,strlen(s)));assert(writes==49);}
 for(const char*s:{"function on_start() while true do end end", "function on_start() while true do pcall(function() while true do end end) end end", "function on_start() while true do pcall(function() string.gsub('a','a',function() while true do end end) end) end end"}){
   LuaRuntime vm(c,32768,3000);assert(vm.load(s,strlen(s)));assert(!vm.start());assert(*vm.error());}
 {LuaRuntime vm(c,24576,10000);const char*s="function on_start() local x=string.rep('x',1000000) end";assert(vm.load(s,strlen(s)));assert(!vm.start());JsonDocument d;vm.describe(d.to<JsonObject>());assert(d["memory_peak"].as<size_t>()<=24576);}
 {LuaRuntime vm(c);const char*s="function on_start() local t={};t.self=t; notes.call('/effect','write',t) end";assert(vm.load(s,strlen(s)));assert(!vm.start());assert(writes==49);}
 {int before=writes;LuaRuntime vm(c,32768,10000,2);const char*s="function on_start() for i=1,10 do notes.call('/effect','write',{value=1}) end end";assert(vm.load(s,strlen(s)));assert(!vm.start());assert(writes==before+2&&strstr(vm.error(),"system_call_budget"));}
 {LuaRuntime vm(c);std::string source="function on_start() return ";source+=std::string(100,'(');source+="1";source+=std::string(100,')');source+=" end";assert(!vm.load(source.c_str(),source.size()));assert(*vm.error());}
 std::cout<<"Runtime source loading, device calls, events, native boundary, instruction yielding, quota failure and cyclic-argument rejection passed\n";
}
