#include "ScriptHost.h"
#include "actuation/Actuator.h"
#include <cassert>
#include <cmath>
#include <iostream>
using namespace notesos;using namespace notesos::actuation;
static Core core;static ScriptHost*runtime;
struct Tracks:Backend {
 double left=0,right=0;bool reached=false;unsigned applications=0,stops=0;
 Limits limits()const override{return {500,2000,false,true};}
 void describe(JsonObject o)const override{o["adapter"]="simulated_tracks";o["command_schema"]["left"]="normalized -1..1";o["command_schema"]["right"]="normalized -1..1";o["physical_validation"]=false;}
 const char*validate(JsonObjectConst a)const override{for(auto p:a)if(strcmp(p.key().c_str(),"left")&&strcmp(p.key().c_str(),"right"))return "unknown_axis";for(auto name:{"left","right"})if(!a[name].is<double>()||!std::isfinite(a[name].as<double>())||std::abs(a[name].as<double>())>1)return "axis_range";return nullptr;}
 const char*apply(JsonObjectConst a)override{left=a["left"];right=a["right"];reached=false;++applications;return nullptr;}
 const char*stop()override{left=right=0;++stops;return nullptr;}
 Observation poll()override{return {reached?Observation::State::Complete:Observation::State::Running,"simulated_feedback",nullptr};}
 void read(JsonObject o)const override{o["commanded_left"]=left;o["commanded_right"]=right;}
};
static Tracks tracks;static Actuator drive(core,tracks,"/dev/drive");
int main(){core.add({"/dev/drive","read,describe,acquire,renew,command,stop,release","",[](const char*m,JsonObjectConst a,JsonObject o){return drive.invoke(m,a,o,core.now());},[](JsonObject o){drive.describe(o);},"read,describe"});
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return core.describe(a["path"]|"",o);},nullptr,"describe"});
 Vfs files;ScriptHost app(core,files);runtime=&app;core.setEventObserver([](const Event&e){return runtime->event(e);});JsonDocument a,out,empty;
 a["source"]=R"lua(function on_start()
  local lease=notes.call('/dev/drive','acquire',{lease_ms=500});assert(lease.ok)
  local r=notes.request('/dev/drive','command',{lease_id=lease.result.lease_id,command={left=0.2,right=0.3}},function(e) assert(e.event=='completed' and e.data.completion.basis=='simulated_feedback') end);assert(r.ok)
 end)lua";a["memory_bytes"]=65536;assert(!app.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(tracks.applications==1);tracks.reached=true;drive.poll(100);uint32_t cursor=0;core.dispatch(cursor);app.tick(100);app.invoke("read",empty.to<JsonObject>(),out.to<JsonObject>());assert(out["callbacks"]==0&&out["fault"]==false);
 assert(!app.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>()));assert(tracks.left==0&&tracks.stops==1);
 auto owner=core.acquireOwner();core.clock(1000);a.clear();assert(!core.invoke("/dev/drive","acquire",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));uint32_t lease=out["lease_id"];a["lease_id"]=lease;a["command"]["left"]=2;a["command"]["right"]=0;
 assert(!strcmp(core.invoke("/dev/drive","command",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"axis_range"));assert(tracks.applications==1&&tracks.left==0);
 a["command"]["left"]=0.5;assert(!core.request("/dev/drive","command",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));uint32_t first=out["completion_id"];
 a["replace"]=true;a["command"]["right"]=-0.5;assert(!core.request("/dev/drive","command",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));uint32_t second=out["completion_id"];
 JsonDocument receipt;assert(!core.completions().read(first,owner,receipt.to<JsonObject>())&&receipt["event"]=="superseded");assert(!core.completions().release(first,owner));
 // Native control scheduling expires a command even when no application pump or
 // network callback runs. The receipt describes a stop command, not measured rest.
 drive.poll(1501);assert(tracks.left==0&&tracks.right==0);assert(!core.completions().read(second,owner,receipt.to<JsonObject>())&&receipt["event"]=="cancelled"&&receipt["data"]["completion"]["basis"]=="driver_stop_issued");core.completions().release(second,owner);
 a.clear();a["lease_id"]=lease;assert(core.invoke("/dev/drive","renew",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(core.resources().promised()==0);
 // User code failure is a native-owner teardown, not a request for that faulty
 // user program to issue a stop itself.
 a.clear();a["source"]=R"lua(function on_start()
 local r=notes.call('/dev/drive','acquire',{});assert(r.ok);assert(notes.call('/dev/drive','command',{lease_id=r.result.lease_id,command={left=0.8,right=0.8}}).ok);while true do end
 end)lua";a["instruction_budget"]=3000;
 assert(app.invoke("load",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(tracks.left==0&&tracks.right==0);app.invoke("stop",empty.as<JsonObjectConst>(),out.to<JsonObject>());assert(core.resources().promised()==0);
 std::cout<<"Actuator runtime calls, atomic validation, native ownership, retarget correlation, independent lease expiry, honest completion and VM-fault stop passed (simulation only)\n";
}
