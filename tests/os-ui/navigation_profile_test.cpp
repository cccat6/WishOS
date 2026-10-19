#include "ScriptHost.h"
#include <cassert>
#include <fstream>
#include <map>
#include <iostream>
using namespace notesos;
static ScriptHost* observed=nullptr;
struct Fixture {
 Core core;Vfs vfs;MemoryFiles memory{"ram","/ram","ram"};ScriptHost host{core,vfs};uint64_t now=0;uint32_t cursor=0,revision=1,shown=1;unsigned count,maxClicks;bool defer=false,hidden=false,ready=true,grid=false,rejectUpdate=false,backFirst=false;std::string focus;std::map<std::string,unsigned>effects;
 Fixture(unsigned keys=1,unsigned clicks=3):count(keys),maxClicks(clicks){vfs.add(memory);observed=&host;core.setEventObserver([](const Event&e){return observed->event(e);});
  core.add({"/dev/buttons","read","",nullptr,nullptr,"read",this,[](void*p,const char*,JsonObjectConst,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);for(unsigned i=0;i<s.count;++i){auto b=o["buttons"]["B"+std::to_string(i)].to<JsonObject>();b["index"]=i;b["applied"]["max_clicks"]=s.maxClicks;}return nullptr;}});
  core.add({"/sys/ui","elements,update","",nullptr,nullptr,"elements",this,[](void*p,const char*m,JsonObjectConst a,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);if(!strcmp(m,"update")){if(s.rejectUpdate)return "ui_memory_budget";s.focus=a["focus"]|"";++s.revision;if(!s.defer)s.shown=s.revision;o["revision"]=s.revision;return nullptr;}if(!s.ready)return "presentation_not_ready";o["view_id"]=1;o["revision"]=s.shown;o["activation"]=1;auto elements=o["elements"].to<JsonArray>();for(auto id:{"one","two","home"}){if(s.hidden&&!strcmp(id,"two"))continue;auto n=elements.add<JsonObject>();n["id"]=id;n["target"]=true;n["x"]=s.grid&&elements.size()==2?100:0;n["y"]=s.grid?(elements.size()==3?80:0):elements.size()*40;}if(s.backFirst)for(auto e:elements)if(e["id"]=="home")e["y"]=0;o["cursor"]=3;o["more"]=false;return nullptr;}});
  core.add({"/sys/timers","write,remove","",nullptr,nullptr,"",this,[](void*p,const char*m,JsonObjectConst a,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);if(!strcmp(m,"write"))return s.core.timer(a,o);s.core.removeTimer(a["id"]|0u);return nullptr;}});
  core.add({"/effect","write","",nullptr,nullptr,"",this,[](void*p,const char*,JsonObjectConst a,JsonObject)->const char*{++static_cast<Fixture*>(p)->effects[a["id"]|""];return nullptr;}});
  std::ifstream input("firmware/WishOS/lib/navigation.lua");std::string text((std::istreambuf_iterator<char>(input)),{});size_t n;assert(!memory.write("/navigation.lua",0,(const uint8_t*)text.data(),text.size(),true,n));
 }
 ~Fixture(){JsonDocument o;host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());assert(!core.nextTimer());assert(!core.resources().promised());core.setEventObserver(nullptr);}
 void load(bool spatial=false){const char*text=R"lua(local N=notes.require('/ram/navigation.lua');local handlers={};local nav
local function call(p,m,a)local r=notes.call(p,m,a or {});if r.ok then return r.result end end
local function timer(d,p,fn)local t=call('/sys/timers','write',{delay_ms=d,period_ms=p});handlers[t.id]=fn;return t.id end
local function cancel(id)handlers[id]=nil;call('/sys/timers','remove',{id=id})end
function on_start()
 notes.on('/sys/timers','fired',function(e)local f=handlers[e.data.id];if f then f()end end)
 local actions={};for _,id in ipairs({'one','two','home'})do actions[id]=function()call('/effect','write',{id=id})end end
 nav=N.bind({call=call,view=function()return 1 end,accept=function(d)return not d.presentation_id or d.presentation_id==1 end,actions=actions,timer=timer,cancel=cancel,config={scan_interval_ms=250,last={'home'}},back=function()call('/effect','write',{id='back'})end})
 N.axis('/dev/slider','axis',{step=.2},function(direction)call('/effect','write',{id=direction})end)
end
function on_stop()nav.stop()end)lua";JsonDocument a,o;std::string program=text;if(spatial){auto pos=program.find("config={scan_interval_ms=250,last={'home'}}");program.replace(pos,std::string("config={scan_interval_ms=250,last={'home'}}").size(),"config={roles={B0={['1']='right'},B1={['1']='down'},B2={['1']='left'},B3={['1']='up'},B4={['1']='activate'}}}");}a["source"]=program;a["memory_bytes"]=98304;a["instruction_budget"]=200000;a["system_call_budget"]=96;auto e=host.invoke("load",a.as<JsonObjectConst>(),o.to<JsonObject>());if(e){serializeJson(o,std::cerr);assert(false);}cursor=core.sequence();}
 void step(unsigned n=1){while(n--){now+=20;core.clock(now);core.tick();core.dispatch(cursor);host.tick(now);JsonDocument o;host.invoke("read",JsonObjectConst(),o.to<JsonObject>());if(o["fault"]==true){serializeJson(o,std::cerr);assert(false);}}}
 void click(unsigned key,unsigned clicks=1,bool consumed=false,unsigned view=1){JsonDocument e;e["button"]="B"+std::to_string(key);e["count"]=clicks;e["system_consumed"]=consumed;e["presentation_id"]=view;core.emit("/dev/buttons","click",e.as<JsonObjectConst>());step();}
 void axis(const char*phase,double position){JsonDocument e;e["phase"]=phase;e["position"]=position;core.emit("/dev/slider","axis",e.as<JsonObjectConst>());step();}
};
int main(){
 {Fixture f;f.load();f.click(0);assert(f.focus=="one");f.click(0,2);assert(f.effects["one"]==1);f.click(0,1,true);assert(f.focus=="one");f.click(0,1,false,99);assert(f.focus=="one");f.click(0);assert(f.focus=="two");f.hidden=true;f.click(0,2);assert(!f.effects["two"]);f.ready=false;f.click(0,2);assert(f.effects["one"]==1);}
 {Fixture f;f.load();f.defer=true;f.click(0);f.click(0,2);assert(!f.effects["one"]);f.shown=f.revision;f.click(0,2);assert(f.effects["one"]==1);}
 {Fixture f;f.load();f.click(0);assert(f.focus=="one");f.rejectUpdate=true;f.click(0);assert(f.focus=="one");f.click(0,2);assert(f.effects["one"]==1&&!f.effects["two"]);}
 {Fixture f;f.backFirst=true;f.load();f.click(0);assert(f.focus=="one");f.click(0);assert(f.focus=="two");f.click(0);assert(f.focus=="home");}
 {Fixture f(2);f.load();f.click(0);f.click(1);assert(f.effects["one"]==1);f.click(1,2);assert(f.effects["back"]==1);}
 {Fixture f(3);f.load();f.click(1);assert(f.focus=="one");f.click(1);assert(f.focus=="two");f.click(0);assert(f.focus=="one");f.click(2);assert(f.effects["one"]==1);}
 {Fixture f(1,1);f.load();f.step(13);assert(f.focus=="one");f.click(0);assert(f.effects["one"]==1);f.step(13);assert(f.focus=="two");f.click(0);assert(f.effects["two"]==1);}
 {Fixture f;f.load();f.axis("begin",.1);f.axis("update",.2);assert(!f.effects["next"]);f.axis("update",.4);assert(f.effects["next"]==1);f.axis("update",.1);assert(f.effects["previous"]==1);f.axis("cancel",.1);f.axis("update",.8);assert(f.effects["next"]==1);}
 {Fixture f(5);f.grid=true;f.load(true);f.click(4);assert(f.focus=="one");f.click(0);assert(f.focus=="two");f.click(1);assert(f.focus=="home");f.click(3);assert(f.focus=="one");f.click(4);assert(f.effects["one"]==1);}
 std::cout<<"Runtime navigation: single/two/three button roles, scan-only selection, visible focus barrier, stale/hidden targets, axis movement and VM cleanup passed\n";
}
