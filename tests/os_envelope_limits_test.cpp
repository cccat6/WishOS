#include "Core.h"
#include "FileService.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace notesos;
int main(){Core core;JsonDocument a,o;core.add({"/target","write","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;}});
 for(int i=0;i<12;++i){a.clear();a["source"]="/target";a["event"]="changed";a["target"]="/target";a["method"]="write";a["args"]["value"]=std::string(700,'x');assert(!core.setRule(a.as<JsonObjectConst>(),o.to<JsonObject>()));}
 unsigned cursor=0,seen=0;do{a.clear();a["cursor"]=cursor;assert(!core.readRules(a.as<JsonObjectConst>(),o.to<JsonObject>()));seen+=o["rules"].size();cursor=o["next_cursor"];assert(measureJson(o)<MAX_WIRE);}while(o["more"]);assert(seen==12);
 a.clear();a["large"]=std::string(460,'x');for(int i=0;i<8;++i)assert(core.emit("/target","changed",a.as<JsonObjectConst>()));cursor=0;seen=0;while(cursor<core.sequence()){core.events(cursor,8,o.to<JsonObject>());assert(measureJson(o)<MAX_WIRE);assert(o["cursor"].as<unsigned>()>cursor);cursor=o["cursor"];seen+=o["events"].size();}assert(seen==8);
 Vfs vfs;MemoryFiles memory("ram","/r","ram");vfs.add(memory);FileService service(vfs);size_t n;for(int i=0;i<12;++i){std::string path="/"+std::string(180,'x')+std::to_string(i);assert(!memory.write(path.c_str(),0,nullptr,0,true,n));}
 a.clear();a["file"]="/r";a["limit"]=24;cursor=seen=0;do{a["cursor"]=cursor;assert(!service.invoke("list",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(measureJson(o)<MAX_WIRE);seen+=o["entries"].size();cursor=o["next_cursor"];}while(o["more"]);assert(seen==12);
 std::cout<<"Large rule/event/file catalogs remain fully retrievable within the control envelope\n";
}
