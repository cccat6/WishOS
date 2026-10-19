#include "SystemNavigation.h"
#include "ButtonGestures.h"
#include <cassert>
#include <vector>
#include <string>
#include <iostream>
using namespace notesos;
struct Fact{std::string name;bool held=false;unsigned count=0;bool consumed=false;};
static void file(MemoryFiles&f,const char*p,const char*s){size_t n;assert(!f.write(p,0,reinterpret_cast<const uint8_t*>(s),strlen(s),true,n));}
int main(){Core core;Vfs vfs;MemoryFiles files("apps","/apps","ram");vfs.add(files);ScriptHost host(core,vfs);Applications apps(core,vfs,host,"launcher","voice");
 for(auto id:{"launcher","voice","user"}){std::string manifest=std::string("{\"format\":\"wishos-app/1\",\"id\":\"")+id+"\",\"title\":\"test\",\"entry\":\"main.lua\"}";file(files,(std::string("/")+id+".json").c_str(),manifest.c_str());}file(files,"/main.lua","function on_start() end");
 assert(apps.defaults(R"([{"id":"launcher","manifest":"/apps/launcher.json"},{"id":"voice","manifest":"/apps/voice.json"},{"id":"user","manifest":"/apps/user.json"}])"));
 auto launch=[&](const char*id){JsonDocument a,o;a["id"]=id;assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();};
 auto state=[&](){JsonDocument o;assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>()));return o;};
 SystemNavigation nav(core,apps,7,1,1);JsonDocument policy;policy["home_clicks"]=3;policy["home_click_window_ms"]=240;policy["debounce_ms"]=25;assert(!nav.configure(policy.as<JsonObjectConst>()));
 std::vector<Fact>facts;ButtonGestures buttons({"USER","UP","DOWN"},&facts,[](void*p,const char*n,JsonObjectConst d){static_cast<std::vector<Fact>*>(p)->push_back({n,d["long"]|false,d["count"]|0u,d["system_consumed"]|false});});
 auto sample=[&](uint32_t mask,uint64_t t){core.clock(t);nav.poll(mask,t);buttons.consumeCurrentPress(nav.consumedButtons());bool levels[]={bool(mask&1),bool(mask&2),bool(mask&4)};buttons.sampleAll(levels,3,uint32_t(t),InputPresentation{});apps.poll();};
 uint64_t t=100;auto tap=[&](){sample(1,t);sample(1,t+30);sample(0,t+100);sample(0,t+130);t+=180;};
 launch("user");tap();tap();assert(state()["current"]=="user");tap();assert(state()["current"]=="launcher");for(unsigned n=0;n<20;++n)sample(0,t+n);assert(state()["current"]=="launcher");
 // Home remains independent of the application's click-count threshold.
 launch("user");JsonDocument a,o;a["defaults"]["max_clicks"]=2;assert(!buttons.configure(a.as<JsonObjectConst>(),o.to<JsonObject>()));t=2000;tap();tap();assert(state()["current"]=="user");tap();assert(state()["current"]=="launcher");
 // A 650 ms protected hold is shorter than the driver's genuine long press.
 // Its release remains a raw short release, with its click consumed.
 launch("user");facts.clear();sample(1,5000);sample(1,5030);sample(1,5601);assert(state()["current"]=="voice");auto request=state()["voice_request_id"].as<uint32_t>();for(unsigned n=0;n<50;++n)sample(1,5602+n);assert(state()["voice_request_id"]==request);sample(0,5650);sample(0,5680);sample(0,6100);assert(state()["voice_input_held"]==false);bool released=false;for(auto&f:facts){assert(f.name!="click");if(f.name=="release"){released=true;assert(!f.held&&f.consumed);}}assert(released);
 // A short click waiting for aggregation is also cleared when the next
 // physical press becomes a protected voice hold before native long=800 ms.
 launch("user");facts.clear();t=7000;tap();for(auto&f:facts)if(f.name=="release")assert(!f.consumed);sample(1,t);sample(1,t+30);sample(1,t+601);sample(0,t+650);sample(0,t+680);sample(0,t+1000);for(auto&f:facts)assert(f.name!="click");
 // Long speech never matures a timed Home action in click mode.
 launch("user");facts.clear();sample(1,10000);sample(1,10030);sample(1,10601);sample(1,310000);assert(state()["current"]=="voice");sample(0,310100);sample(0,310130);sample(0,310500);for(auto&f:facts)assert(f.name!="click");
 // Validation and rejected changes preserve the held route. A legal change
 // releases the old recipient before resetting the mapping.
 launch("user");sample(1,320000);sample(1,320030);sample(1,320601);assert(state()["voice_input_held"]==true);a.clear();a["home_click_window_ms"]=300;assert(!nav.check(a.as<JsonObjectConst>()));assert(state()["voice_input_held"]==true);a["home_buttons"]=0;assert(nav.configure(a.as<JsonObjectConst>()));assert(state()["voice_input_held"]==true);a.remove("home_buttons");assert(!nav.configure(a.as<JsonObjectConst>()));assert(state()["voice_input_held"]==false);sample(0,320650);sample(0,320680);
 // Delayed main-loop acceptance anchors the hold to the captured raw edge,
 // and consumption survives ButtonGestures accepting that press afterward.
 launch("user");facts.clear();sample(1,330000);sample(1,330650);assert(state()["current"]=="voice");sample(0,330700);sample(0,330730);sample(0,331000);for(auto&f:facts)assert(f.name!="click");
 // A queued short release cannot become voice or held Home merely because
 // the event loop is late. Independent multi-key legacy hold still works.
 launch("user");sample(1,340000);sample(1,340180);sample(0,340180);sample(0,341000);assert(state()["current"]=="user");
 a.clear();a["home_clicks"]=0;a["home_buttons"]=6;a["voice_buttons"]=1;a["home_hold_ms"]=1800;assert(!nav.configure(a.as<JsonObjectConst>()));sample(6,350000);sample(6,350030);sample(6,351801);assert(state()["current"]=="launcher");sample(0,351900);sample(0,351930);
 host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());assert(core.resources().promised()==0);std::cout<<"Protected triple Home, independent application clicks, consumed voice release, delayed edges, legal reconfiguration release and legacy multi-key hold passed\n";
}
