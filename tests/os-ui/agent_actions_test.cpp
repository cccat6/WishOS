#include "AgentService.h"
#include "RemoteRequests.h"
#include <cassert>
#include <iostream>
#include <vector>
#include <fstream>
#include <iterator>
using namespace notesos;
static ScriptHost* observedHost=nullptr;
static JsonDocument json(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
struct ObservedMemoryFiles:MemoryFiles{unsigned reads=0;ObservedMemoryFiles():MemoryFiles("ram","/ram","ram"){}const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{++reads;return MemoryFiles::read(p,at,b,n,got);}};
struct Fixture {
 Core core;Vfs fs;ObservedMemoryFiles files;AssetTransfers assets{core,fs,"boot"};RemoteRequests remote{core};AgentService ai{core,assets,fs};
 ScriptHost*host=nullptr;unsigned writes=0;uint32_t current=0,native=0;bool busy=false;std::vector<int>values;
 Fixture(){fs.add(files);assert(assets.restore(R"({"targets":[{"provider":"ram","root":"/downloads","quota_bytes":"100000"}]})"));current=core.acquireOwner();
  core.add(Device::bind("/services/ai",AgentService::Methods,"",ai,"read,read_action,read_result"));
  core.add({"/services/remote","submit,read,peek,complete,release","",nullptr,nullptr,"read,peek",this,[](void*p,const char*m,JsonObjectConst a,JsonObject o){auto&s=*static_cast<Fixture*>(p);return s.remote.invoke(m,a,o,s.core.now(),"boot");}});
  core.add({"/sys/runtime","read","",nullptr,nullptr,"read",this,[](void*p,const char*,JsonObjectConst,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);if(s.host)return s.host->invoke("read",JsonObjectConst(),o);o["loaded"]=s.current!=0;o["owner"]=s.current;return nullptr;}});
  core.add({"/test","write,replace,refresh,fail","",nullptr,[](JsonObject o){asyncContract(o,"refresh","/test",{"finished"})["operation_id"]="operation_id";},"",this,[](void*p,const char*m,JsonObjectConst a,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);if(!strcmp(m,"fail")){o["detail"]="allocation refused";return "memory_low";}else if(!strcmp(m,"replace")){auto old=s.current;s.current=s.core.acquireOwner();s.core.releaseOwner(old);o["new_owner"]=s.current;}else if(!strcmp(m,"refresh")){assert(!s.busy);s.busy=true;o["operation_id"]=++s.native;}else{++s.writes;s.values.push_back(a["value"]|0);o["value"]=s.writes;}return nullptr;}});
 }
 uint32_t submit(uint32_t owner=0){auto a=json(R"({"input":{"text":"work"}})");JsonDocument o;assert(!core.invoke("/services/ai","submit",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner?owner:current));return o["id"];}
 void respond(const char*text){JsonDocument next,o;assert(!remote.invoke("peek",JsonObjectConst(),next.to<JsonObject>(),core.now(),"boot"));auto a=json(text);JsonDocument result;result["id"]=next["id"];result["ok"]=true;result["result"]=a;assert(!remote.invoke("complete",result.as<JsonObjectConst>(),o.to<JsonObject>(),core.now(),"boot"));}
 void polls(unsigned n=15){for(unsigned i=0;i<n;++i)ai.poll();}
 JsonDocument read(uint32_t id){JsonDocument a,o;a["id"]=id;assert(!ai.invoke("read",a.as<JsonObjectConst>(),o.to<JsonObject>()));return o;}
 void release(uint32_t id){JsonDocument a,o;a["id"]=id;assert(!ai.invoke("release",a.as<JsonObjectConst>(),o.to<JsonObject>()));}
 void ack(uint32_t id,uint32_t step,uint32_t owner){JsonDocument a,o;a["id"]=id;a["step"]=step;a["ok"]=true;a["result"]["value"]=91;assert(!core.invoke("/services/ai","acknowledge",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));}
 void complete(){JsonDocument d;d["operation_id"]=native;d["ok"]=true;busy=false;core.emit("/test","finished",d.as<JsonObjectConst>());}
 uint32_t stage(const std::string&body){JsonDocument a,o;a["bytes"]=std::to_string(body.size());a["files"]=1;assert(!assets.invoke("begin",a.as<JsonObjectConst>(),o.to<JsonObject>()));uint32_t id=o["id"];Sha256 h;h.update((const uint8_t*)body.data(),body.size());char sha[65];h.finish(sha);a.clear();a["id"]=id;a["path"]="actions.jsonl";a["bytes"]=std::to_string(body.size());a["sha256"]=sha;assert(!assets.invoke("file",a.as<JsonObjectConst>(),o.to<JsonObject>()));for(size_t at=0;at<body.size();at+=1024){size_t n=std::min(size_t(1024),body.size()-at);char encoded[2049];hex((const uint8_t*)body.data()+at,n,encoded);a.clear();a["id"]=id;a["offset"]=std::to_string(at);a["hex"]=encoded;assert(!assets.invoke("chunk",a.as<JsonObjectConst>(),o.to<JsonObject>()));}a.clear();a["id"]=id;assert(!assets.invoke("close_file",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!assets.invoke("commit",a.as<JsonObjectConst>(),o.to<JsonObject>()));return id;}
};
int main(){
 {
 Fixture f;auto id=f.submit();f.respond(R"({"format":"wishos-result/2","actions":[{"type":"system","path":"/test","method":"write","args":{"value":3}},{"type":"application","topic":"result","data":{"count":{"$previous":"value"}}},{"type":"system","path":"/test","method":"write","args":{"value":{"$previous":"value"}}}]})");f.polls();assert(f.writes==1&&f.read(id)["state"]=="awaiting_application");JsonDocument a,o;a["id"]=id;a["step"]=1;assert(!f.core.invoke("/services/ai","read_action",a.as<JsonObjectConst>(),o.to<JsonObject>(),f.current));assert(o["action"]["data"]["count"]==1);a["ok"]=true;assert(f.core.invoke("/services/ai","acknowledge",a.as<JsonObjectConst>(),o.to<JsonObject>(),0));f.ack(id,1,f.current);f.polls();assert(f.writes==2&&f.values.back()==91&&f.read(id)["state"]=="completed");
 }
 {
 Fixture f;auto id=f.submit();f.respond(R"({"format":"wishos-result/2","actions":[{"type":"system","path":"/test","method":"fail","args":{}},{"type":"system","path":"/test","method":"write","args":{}}]})");f.polls();assert(f.read(id)["error"]=="memory_low"&&f.read(id)["step"]==0&&f.writes==0);JsonDocument a,o;a["id"]=id;assert(!f.ai.invoke("read_result",a.as<JsonObjectConst>(),o.to<JsonObject>()));uint8_t bytes[512];size_t n;assert(unhex(o["hex"],bytes,sizeof bytes,n));JsonDocument result;assert(!deserializeJson(result,bytes,n));assert(result["detail"]=="allocation refused");
 }
 {
 Fixture f;auto origin=f.current,id=f.submit();f.respond(R"({"format":"wishos-result/2","actions":[{"type":"application","data":{"text":"ready"}},{"type":"system","path":"/test","method":"replace","args":{}},{"type":"application","recipient":"foreground","data":{"text":"open"}}]})");f.polls();assert(f.current==origin);f.ack(id,0,origin);f.polls();assert(f.current!=origin&&f.read(id)["state"]=="awaiting_application");f.ack(id,2,f.current);f.polls();assert(f.read(id)["state"]=="completed");
 }
 {
 Fixture f;auto id=f.submit();f.respond(R"({"format":"wishos-result/2","actions":[{"type":"system","path":"/test","method":"replace","args":{}},{"type":"application","recipient":"origin","data":{}}]})");f.polls();assert(f.read(id)["error"]=="ai_application_unavailable");
 auto invalid=f.submit();f.respond(R"({"format":"wishos-result/2","actions":[{"type":"system","path":"/test","method":"write","args":{}},{"type":"system","path":"/missing","method":"write","args":{}}]})");f.polls();assert(f.writes==0&&f.read(invalid)["state"]=="failed");
 }
 {
 Fixture f;auto id=f.submit();f.respond(R"({"format":"wishos-result/2","actions":[{"type":"system","path":"/test","method":"refresh","args":{}},{"type":"system","path":"/test","method":"write","args":{}}]})");f.polls();assert(f.busy&&f.writes==0);JsonDocument a,o;a["id"]=id;assert(!f.ai.invoke("cancel",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(f.read(id)["state"]=="awaiting_system");f.complete();f.polls();assert(f.writes==0&&f.read(id)["error"]=="cancelled");
 }
 {
 Fixture f;std::string plan;for(int i=0;i<80;++i)plan+="{\"type\":\"system\",\"path\":\"/test\",\"method\":\"write\",\"args\":{\"value\":"+std::to_string(i)+"}}\n";auto tid=f.stage(plan),id=f.submit();std::string r="{\"format\":\"wishos-result/2\",\"operation_id\":\"large\",\"transfer_id\":"+std::to_string(tid)+",\"sequence\":{\"path\":\"actions.jsonl\",\"format\":\"jsonl\"}}";f.respond(r.c_str());f.polls(90);assert(f.writes>0&&f.writes<80);f.polls(100);assert(f.writes==80&&f.values.back()==79&&f.read(id)["state"]=="completed");assert(f.core.resources().promised()==0);f.release(id);
 auto duplicate=f.submit();f.respond(r.c_str());f.polls();assert(f.writes==80&&f.read(duplicate)["state"]=="completed");
 }
 {
 Fixture f;auto id=f.submit();f.core.releaseOwner(f.current);f.current=0;f.respond(R"({"format":"wishos-result/2","actions":[{"type":"system","path":"/test","method":"write","args":{}}]})");f.polls();assert(f.writes==1&&f.read(id)["state"]=="completed");
 }
 {
 Fixture f;auto first=f.submit(),second=f.submit();const char*plan=R"({"format":"wishos-result/2","operation_id":"shared","actions":[{"type":"system","path":"/test","method":"refresh","args":{}}]})";f.respond(plan);f.polls();f.respond(plan);f.polls();assert(f.read(second)["state"]=="following");JsonDocument a,o;a["id"]=second;assert(!f.ai.invoke("cancel",a.as<JsonObjectConst>(),o.to<JsonObject>()));f.complete();f.polls();assert(f.read(first)["state"]=="completed"&&f.read(second)["error"]=="cancelled");f.release(first);f.release(second);auto retry=f.submit();f.respond(plan);f.polls();assert(!f.busy&&f.native==1&&f.read(retry)["state"]=="completed");
 }
 {
 Fixture f;std::string plan;for(int i=0;i<80;++i)plan+="{\"type\":\"system\",\"path\":\"/test\",\"method\":\"write\",\"args\":{}}\n";
 auto tid=f.stage(plan),id=f.submit();auto response="{\"format\":\"wishos-result/2\",\"transfer_id\":"+std::to_string(tid)+",\"sequence\":{\"path\":\"actions.jsonl\",\"format\":\"jsonl\"}}";f.respond(response.c_str());f.polls(3);assert(f.read(id)["state"]=="validating");auto held=f.core.resources().promised();assert(held>0);JsonDocument a,o;a["id"]=id;assert(!f.ai.invoke("cancel",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(f.writes==0&&f.core.resources().promised()<held);auto next=f.stage(plan);assert(next!=tid);
 }
 {
 Fixture f;ScriptHost host(f.core,f.fs);f.host=&host;observedHost=&host;f.core.setEventObserver([](const Event&e){return observedHost?observedHost->event(e):false;});
 f.core.add({"/sys/timers","write,remove","",nullptr,nullptr,"",&f,[](void*p,const char*m,JsonObjectConst a,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);if(!strcmp(m,"remove")){s.core.removeTimer(a["id"]);return nullptr;}return s.core.timer(a,o);}});
 std::ifstream input("firmware/WishOS/lib/ai.lua");assert(input);std::string library((std::istreambuf_iterator<char>(input)),{});size_t wrote;assert(!f.files.write("/ai.lua",0,(const uint8_t*)library.data(),library.size(),true,wrote));
 const char*source="local ai=notes.require('./ai.lua') function on_start() receiver=ai.listen(function(a) assert(notes.call('/test','write',{value=a.data.value}).ok) end) end";
 assert(!f.files.write("/main.lua",0,(const uint8_t*)source,strlen(source),true,wrote));JsonDocument a,o;a["file"]="/ram/main.lua";a["memory_bytes"]=65536;assert(!host.invoke("load",a.as<JsonObjectConst>(),o.to<JsonObject>()));auto owner=o["owner"].as<uint32_t>();auto id=f.submit(owner);uint32_t cursor=f.core.sequence();
 f.respond(R"({"format":"wishos-result/2","actions":[{"type":"application","data":{"value":17}}]})");f.polls();assert(f.read(id)["state"]=="awaiting_application");JsonDocument noise;for(int i=0;i<100;++i)f.core.emit("/noise","message",noise.to<JsonObject>());f.core.dispatch(cursor);assert(f.writes==0);f.core.clock(1000);f.core.tick();f.core.dispatch(cursor);host.tick(1000);f.polls();assert(f.writes==1&&f.values.back()==17&&f.read(id)["state"]=="completed");f.core.clock(2000);f.core.tick();f.core.dispatch(cursor);assert(f.writes==1);host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());observedHost=nullptr;
 }
 {
 Fixture f;std::string plan(4096,'\n');plan+="{\"type\":\"system\",\"path\":\"/test\",\"method\":\"write\",\"args\":{}}\n";
 auto transfer=f.stage(plan),id=f.submit();auto result="{\"format\":\"wishos-result/2\",\"transfer_id\":"+std::to_string(transfer)+",\"sequence\":{\"path\":\"actions.jsonl\",\"format\":\"jsonl\"}}";f.respond(result.c_str());f.ai.poll();
 for(unsigned turn=0;turn<10;++turn){f.files.reads=0;f.ai.poll();assert(f.files.reads<=1&&f.writes==0);}
 f.polls(100);assert(f.writes==1&&f.read(id)["state"]=="completed");
 }
 std::cout<<"Ordered application/system actions, acknowledgement barriers, instance replacement, native cancellation, preflight and incremental long plans passed\n";
}
