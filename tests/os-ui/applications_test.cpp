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
 JsonDocument a,o;a["manifest"]="/apps/one.json";assert(!apps.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()));a.clear();a["id"]="one";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!host.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["loaded"]==false);apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="one");core.listTimers(o["timers"].to<JsonArray>());assert(o["timers"].size()==1);
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
 // A queued activation uses the admitted manifest path even if the catalog is
 // subsequently updated. Direct runtime stop does not leave an app marked in use.
 write(memory,"/alternate.json",R"({"format":"wishos-app/1","id":"on-card","title":"New revision","entry":"missing.lua"})");a.clear();a["id"]="on-card";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));a.clear();a["manifest"]="/apps/alternate.json";assert(!apps.invoke("install",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="on-card");
 host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());a.clear();a["id"]="on-card";assert(!apps.invoke("remove",a.as<JsonObjectConst>(),o.to<JsonObject>()));
 // on_start may fail after its candidate replaces the old VM. The manager
 // still knows which instance faulted and restores the launcher on the next poll.
 write(memory,"/one.lua","function on_start() error('failed start') end");a.clear();a["id"]="one";assert(!apps.invoke("launch",a.as<JsonObjectConst>(),o.to<JsonObject>()));apps.poll();apps.poll();apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["current"]=="launcher");
 // A user-selected launcher disappearing must not remove the recovery route.
 a.clear();a["launcher"]="one";assert(!apps.invoke("configure",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!memory.remove("/one.json"));apps.requestHome();apps.poll();apps.poll();assert(!apps.invoke("read",JsonObjectConst(),o.to<JsonObject>())&&o["current"]=="launcher");
 // A broken factory launcher must stop in recoverable control state, not
 // repeatedly recreate a faulted VM on every loop iteration.
 write(memory,"/home.lua","function on_start() error('broken factory UI') end");apps.requestHome();apps.poll();apps.poll();auto sequence=core.sequence();for(unsigned i=0;i<100;++i)apps.poll();assert(core.sequence()==sequence);apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["recovery_required"]==true&&o["runtime"]==false);write(memory,"/home.lua","function on_start() print('recovered') end");apps.requestHome();apps.poll();apps.poll();apps.invoke("read",JsonObjectConst(),o.to<JsonObject>());assert(o["recovery_required"]==false&&o["current"]=="launcher");
 host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());std::cout<<"Installed application references, deferred activation, replaceable launcher and independent recovery input passed\n";
}
