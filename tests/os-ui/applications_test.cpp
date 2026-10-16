#include "Applications.h"
#include "SystemNavigation.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static void write(MemoryFiles&f,const char*path,const char*data){size_t n;assert(!f.write(path,0,(const uint8_t*)data,strlen(data),true,n));}
struct Mountable:MemoryFiles {bool ready=true;Mountable():MemoryFiles("removable","/card","ram"){}bool available()const override{return ready;}void describe(JsonObject o)const override{MemoryFiles::describe(o);o["prepare"]["path"]="/card-device";o["prepare"]["method"]="mount";o["prepare"]["args"].to<JsonObject>();}};
static Applications*manager;static Core*systemCore;
int main(){Core core;systemCore=&core;Vfs vfs;MemoryFiles memory("apps","/apps","ram");vfs.add(memory);ScriptHost host(core,vfs);
 write(memory,"/home.json",R"({"format":"wishos-app/1","id":"launcher","title":"Launcher","entry":"home.lua"})");write(memory,"/home.lua","function on_start() print('home') end");write(memory,"/one.json",R"({"format":"wishos-app/1","id":"one","title":"User application","entry":"one.lua"})");write(memory,"/one.lua","function on_start() assert(notes.call('/sys/timers','write',{delay_ms=100,period_ms=100}).ok) end function on_stop() while true do end end");
 Applications apps(core,vfs,host,"launcher");manager=&apps;assert(apps.defaults(R"([{"id":"launcher","title":"Launcher","manifest":"/apps/home.json"}])"));
 core.add({"/sys/apps",Applications::Methods,"",[](const char*m,JsonObjectConst a,JsonObject o){return manager->invoke(m,a,o);},[](JsonObject o){manager->describe(o);},"read,list"});core.add({"/sys/timers","write,read","",[](const char*,JsonObjectConst a,JsonObject o)->const char*{return systemCore->timer(a,o);}});
 JsonDocument a,o;a["manifest"]="/apps/one.json";assert(!apps.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()));a.clear();a["id"]="one";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!host.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["loaded"]==false);for(auto method:{"load","restart","stop"})assert(!strcmp(apps.runtimeInvoke(method,JsonObjectConst(),o.to<JsonObject>()),"application_transition_pending"));assert(!apps.runtimeInvoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["loaded"]==false);apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="one");core.listTimers(o["timers"].to<JsonArray>());assert(o["timers"].size()==1);
 // Escaping does not depend on the foreground program's event handler.
 SystemNavigation nav(core,apps,3,1,2);nav.poll(1,0);nav.poll(1,40);nav.poll(1,2000);apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="launcher");core.listTimers(o["timers"].to<JsonArray>());assert(o["timers"].size()==0);
 // Applications cannot remove the protected recovery binding; invalid system
 // configuration also leaves the old mapping intact.
 core.add(Device::bind("/sys/navigation","read,configure","recovery",nav));uint32_t actor=core.acquireOwner();a.clear();a["home_buttons"]=0;
 assert(!strcmp(core.invoke("/sys/navigation","configure",a.as<JsonObjectConst>(),o.to<JsonObject>(),actor),"system_configuration_required"));assert(nav.configure(a.as<JsonObjectConst>()));nav.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["home_buttons"]==1);core.releaseOwner(actor);
 // Switching from inside a callback is queued, never destroys its own VM stack.
 write(memory,"/one.lua","function on_start() assert(notes.call('/sys/apps','read').result.arguments.message=='hello');assert(notes.call('/sys/apps','home').ok) end");a.clear();a["id"]="one";a["arguments"]["message"]="hello";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="launcher");
 // A different storage adapter declares its own completion event, without an
 // `ok` field. Application startup follows the capability contract, not SD names.
 Mountable card;vfs.add(card);write(card,"/app.json",R"({"format":"wishos-app/1","id":"on-card","title":"Card app","entry":"main.lua"})");write(card,"/main.lua","function on_start() end");
 core.add({"/card-device","mount","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["operation_id"]=77;return nullptr;},[](JsonObject o){asyncContract(o,"mount","/card-device",{"volume_ready","bad_volume"})["operation_id"]="operation_id";}});
 a.clear();a["manifest"]="/card/app.json";assert(!apps.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()));card.ready=false;a.clear();a["id"]="on-card";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();assert(apps.transitionPending());card.ready=true;JsonDocument mounted;mounted["operation_id"]=77;core.emit("/card-device","volume_ready",mounted.as<JsonObjectConst>());apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="on-card");apps.requestHome();apps.poll();
 // Mount failure preserves the old foreground; a protected home supersedes a
 // pending mount and a late completion cannot launch the abandoned application.
 card.ready=false;a.clear();a["id"]="on-card";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();mounted["error"]="missing_card";core.emit("/card-device","bad_volume",mounted.as<JsonObjectConst>());apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="launcher"&&o["last_error"]=="missing_card");
 assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();apps.requestHome();apps.poll();card.ready=true;core.emit("/card-device","volume_ready",mounted.as<JsonObjectConst>());apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="launcher");
 // Restore and online changes share catalog validation; failed persistence
 // cannot change the launcher. Duplicate entries cannot shadow installed IDs.
 a.clear();a["launcher"]="one";a["persist"]=true;assert(apps.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["launcher"]=="launcher");
 assert(!apps.restore(R"({"launcher":"launcher","voice":"","applications":[{"id":"one","title":"1","manifest":"/apps/one.json"},{"id":"one","title":"2","manifest":"/apps/home.json"}]})"));
 apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["launcher"]=="launcher"&&o["restore_valid"]==false);
 assert(!apps.restore(R"({"launcher":"","voice":"","applications":[]})"));apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["launcher"]=="launcher");
 // A queued activation uses the admitted manifest path even if the catalog is
 // subsequently updated. Direct runtime stop does not leave an app marked in use.
 write(memory,"/alternate.json",R"({"format":"wishos-app/1","id":"on-card","title":"New revision","entry":"one.lua"})");a.clear();a["id"]="on-card";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));a.clear();a["manifest"]="/apps/alternate.json";assert(!apps.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="on-card");
 host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());a.clear();a["id"]="on-card";assert(!apps.invoke("remove",a.as<JsonObjectConst>(),o.to<JsonObject>()));
 // on_start may fail after its candidate replaces the old VM. The manager
 // still knows which instance faulted and restores the launcher on the next poll.
 write(memory,"/one.lua","function on_start() error('failed start') end");a.clear();a["id"]="one";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["last_error"]=="script_start_failed"&&strstr(o["last_detail"]|"","failed start"));apps.poll();apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="launcher");
 // A user-selected launcher disappearing must not remove the recovery route.
 a.clear();a["launcher"]="one";assert(!apps.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!memory.remove("/one.json"));apps.requestHome();apps.poll();apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="launcher");
 // A broken factory launcher must stop in recoverable control state, not
 // repeatedly recreate a faulted VM on every loop iteration.
 write(memory,"/home.lua","function on_start() error('broken factory UI') end");apps.requestHome();apps.poll();apps.poll();auto sequence=core.sequence();for(unsigned i=0;i<100;++i)apps.poll();assert(core.sequence()==sequence);apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["recovery_required"]==true&&o["runtime"]==false);write(memory,"/home.lua","function on_start() print('recovered') end");apps.requestHome();apps.poll();apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["recovery_required"]==false&&o["current"]=="launcher");
 // Install policy follows selected volume identifiers, not MCU or Flash size.
 MemoryFiles internal("flash","/flash","flash",true);vfs.add(internal);
 write(internal,"/user.json",R"({"format":"wishos-app/1","id":"extra","title":"Extra","entry":"main.lua"})");write(internal,"/main.lua","function on_start() end");
 { Applications limited(core,vfs,host,"launcher","",8192,"removable");assert(limited.defaults(R"([{"id":"launcher","title":"Launcher","manifest":"/apps/home.json"}])"));
   a.clear();a["manifest"]="/flash/user.json";assert(!strcmp(limited.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()),"application_provider_not_allowed"));
   assert(!limited.restore(R"({"launcher":"extra","voice":"","applications":[{"id":"extra","title":"Extra","manifest":"/flash/user.json"}]})"));
   limited.describe(o.to<JsonObject>());assert(o["install_providers"][0]=="removable"&&o["factory_launcher"]=="launcher");
   write(card,"/redirect.json",R"({"format":"wishos-app/1","id":"redirect","title":"Redirect","entry":"/flash/main.lua"})");a["manifest"]="/card/redirect.json";assert(!strcmp(limited.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()),"application_provider_not_allowed"));
   a.clear();a["install_providers"].to<JsonArray>().add("flash");assert(!limited.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));a.clear();a["manifest"]="/flash/user.json";assert(!limited.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()));
   a.clear();a["install_providers"].to<JsonArray>().add("removable");assert(!strcmp(limited.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()),"application_provider_not_allowed"));limited.describe(o.to<JsonObject>());assert(o["install_providers"][0]=="flash");
   a["install_providers"][0]="absent";assert(!strcmp(limited.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()),"application_install_provider_unavailable"));limited.describe(o.to<JsonObject>());assert(o["install_providers"][0]=="flash");
   assert(limited.restore(R"({"launcher":"launcher","voice":"","install_providers":["flash","absent"],"applications":[]})")==false);limited.describe(o.to<JsonObject>());assert(o["install_providers"][0]=="flash");

 }
 { Applications expanded(core,vfs,host,"launcher","",8192,"removable,flash");assert(expanded.defaults(R"([{"id":"launcher","title":"Launcher","manifest":"/apps/home.json"}])"));a.clear();a["manifest"]="/flash/user.json";if(auto e=expanded.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>())){std::cerr<<e<<"\n";assert(false);} }
 // Mutable factory files use an ordinary runtime package. A missing or broken
 // replacement falls back once to its seed, while a failed voice entry restores
 // a reachable home after releasing the former foreground instance.
 { host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());
   write(memory,"/seed.json",R"({"format":"wishos-app/1","id":"launcher","title":"Recovery","entry":"home.lua"})");
   write(memory,"/voice.json",R"({"format":"wishos-app/1","id":"voice","title":"Voice","entry":"missing.lua"})");
   Applications managed(core,vfs,host,"launcher","voice");assert(managed.defaults(R"([{"id":"launcher","title":"Home","manifest":"/apps/missing-home.json","fallback_manifest":"/apps/seed.json"},{"id":"voice","title":"Voice","manifest":"/apps/voice.json"}])"));
   managed.requestHome();for(unsigned i=0;i<4;++i)managed.poll();managed.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="launcher"&&o["runtime"]==true);
   managed.requestVoice();for(unsigned i=0;i<8;++i)managed.poll();managed.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="launcher"&&o["runtime"]==true);
 }
 host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());std::cout<<"Installed application references, deferred activation, replaceable launcher and independent recovery input passed\n";
}
