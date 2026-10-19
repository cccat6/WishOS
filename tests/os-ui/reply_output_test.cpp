#include "ScriptHost.h"
#include "FileService.h"
#include "audio/Playback.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>
#include <iostream>
using namespace notesos;
using namespace notesos::audio;
static ScriptHost* observer=nullptr;
struct Output final:OutputBackend{
 bool openState=false,drainedState=false,blocked=false;size_t count=0;
 void describe(JsonObject o)const override{o["sample_rate_hz"]=16000;}
 const char*validate(PcmFormat f)const override{return f.rate==16000&&f.channels==1?nullptr:"audio_format";}
 const char*open(PcmFormat)override{openState=true;return nullptr;}
 const char*write(const int16_t*,size_t n,size_t&accepted)override{accepted=blocked?0:n;count+=accepted;return nullptr;}
 bool drained()override{return drainedState;}void close()override{openState=false;}
};
struct Fixture{
 Core core;Vfs vfs;MemoryFiles memory{"ram","/ram","ram"};FileService fs{vfs,&core};Output output;Playback playback{core,output};ScriptHost host{core,vfs};uint32_t cursor=0;uint64_t now=0;unsigned acknowledgements=0,failures=0;
 Fixture(){memory.budget(262144);vfs.add(memory);observer=&host;core.setEventObserver([](const Event&e){return observer->event(e);});
  core.add(Device::bind("/fs",FileService::Methods,"",fs,"read,stat,list"));
  core.add({"/dev/speaker","describe,read,start,write,finish,stop","",nullptr,[](JsonObject o){asyncContract(o,"start","/dev/speaker")["playback_id"]="playback_id";},"read,describe",this,[](void*p,const char*m,JsonObjectConst a,JsonObject o){return static_cast<Fixture*>(p)->playback.invoke(m,a,o);}});
  core.add({"/sys/capabilities","describe","",nullptr,nullptr,"describe",this,[](void*p,const char*,JsonObjectConst a,JsonObject o){return static_cast<Fixture*>(p)->core.describe(a["path"]|"",o);}});
  core.add({"/sys/timers","write,remove","",nullptr,nullptr,"",this,[](void*p,const char*m,JsonObjectConst a,JsonObject o)->const char*{auto&s=*static_cast<Fixture*>(p);if(!strcmp(m,"remove")){s.core.removeTimer(a["id"]);return nullptr;}return s.core.timer(a,o);}});
  core.add({"/test","write","",nullptr,nullptr,"",this,[](void*p,const char*,JsonObjectConst a,JsonObject)->const char*{auto&s=*static_cast<Fixture*>(p);if(a["ack"]==true)++s.acknowledgements;if(a["status"]=="unavailable")++s.failures;return nullptr;}});
  std::ifstream in("hardware/agent-os/runtime/lckfb-suite/assets/suite/reply.lua");std::string source((std::istreambuf_iterator<char>(in)),{});size_t wrote;assert(!memory.write("/reply.lua",0,(const uint8_t*)source.data(),source.size(),true,wrote));
 }
 ~Fixture(){JsonDocument o;host.invoke("stop",JsonObjectConst(),o.to<JsonObject>());playback.pump();core.setEventObserver(nullptr);observer=nullptr;}
 void audio(unsigned blocks,unsigned tail=640){std::vector<uint8_t>data;for(unsigned sequence=0;sequence<blocks;++sequence){unsigned samples=sequence+1==blocks?tail:640;std::vector<uint8_t>packet(11+(samples+1)/2,0x11);packet[0]=0x42;for(unsigned i=0;i<4;++i)packet[i+1]=uint8_t(sequence>>(8*i));packet[5]=packet[6]=packet[7]=packet[8]=0;packet[9]=uint8_t(samples);packet[10]=uint8_t(samples>>8);data.insert(data.end(),packet.begin(),packet.end());}size_t wrote;assert(!memory.write("/audio.adpcm",0,data.data(),data.size(),true,wrote));}
 void load(const char*mode="speech",const char*file="/ram/audio.adpcm"){
  std::string source=R"lua(local R=notes.require('/ram/reply.lua');local handlers={};local C={config={},preferences={reply_mode='MODE'}}
 function C.call(p,m,a)local r=notes.call(p,m or 'read',a or {});if r.ok then return r.result end;return nil,r.error end
 function C.t(a,b)return b end
 function C.timer(delay,period,fn)local r=C.call('/sys/timers','write',{delay_ms=delay,period_ms=period});handlers[r.id]=fn;return r.id end
 function C.cancel(id)handlers[id]=nil;C.call('/sys/timers','remove',{id=id})end
 function C.change_preferences(v)for k,x in pairs(v)do C.preferences[k]=x end;return {}end
 function on_start()
  notes.on('/sys/timers','fired',function(e)local fn=handlers[e.data.id];if fn then fn()end end)
  notes.on('/test','stop',function()R.stop()end)
  R.init(C,function(status)notes.call('/test','write',{status=status})end)
  local deferred=R.play({format='wishos-audio/1',encoding='ima-adpcm-block',sample_rate_hz=16000,channels=1,packet_bytes=331,file='FILE'},function(ok)assert(ok);notes.call('/test','write',{ack=true})end)
  if deferred~=false then notes.call('/test','write',{ack=true})end
 end
 function on_stop()R.close()end)lua";
  source.replace(source.find("MODE"),4,mode);source.replace(source.find("FILE"),4,file);JsonDocument a,o;a["source"]=source;a["memory_bytes"]=131072;a["instruction_budget"]=200000;a["system_call_budget"]=96;auto e=host.invoke("load",a.as<JsonObjectConst>(),o.to<JsonObject>());if(e){serializeJson(o,std::cerr);std::cerr<<e;abort();}cursor=core.sequence();
 }
 void step(unsigned count=1){while(count--){now+=20;core.clock(now);core.tick();core.dispatch(cursor);host.tick(now);playback.pump();JsonDocument o;host.invoke("read",JsonObjectConst(),o.to<JsonObject>());if(o["fault"]==true){serializeJson(o,std::cerr);abort();}}}
};
int main(){
 {Fixture f;f.audio(3,123);f.load();f.step(30);assert(f.output.count==2*640+123&&f.acknowledgements==0&&f.playback.active());f.output.drainedState=true;f.step(3);assert(f.acknowledgements==1&&!f.failures&&!f.playback.active()&&!f.output.openState);}
 {Fixture f;f.audio(24);f.output.blocked=true;f.load();f.step(30);JsonDocument status;assert(!f.playback.invoke("read",JsonObjectConst(),status.to<JsonObject>()));assert(status["queued_blocks"]==16&&f.acknowledgements==0);f.output.blocked=false;f.output.drainedState=true;f.step(80);assert(f.output.count==24*640&&f.acknowledgements==1&&!f.playback.active());}
 {Fixture f;f.audio(12);f.output.blocked=true;f.load();f.step(3);JsonDocument d;f.core.emit("/test","stop",d.to<JsonObject>());f.step(5);assert(f.acknowledgements==1&&!f.failures&&!f.playback.active()&&!f.output.openState);}
 {Fixture f;f.audio(12);f.output.blocked=true;f.load();f.step(3);JsonDocument o;assert(!f.host.invoke("stop",JsonObjectConst(),o.to<JsonObject>()));f.playback.pump();assert(!f.playback.active()&&!f.output.openState&&!f.core.resources().promised());}
 {Fixture f;f.audio(2);f.load("text");f.step(3);assert(f.acknowledgements==1&&!f.playback.active()&&!f.output.count);}
 {Fixture f;f.load("speech","/ram/missing.adpcm");f.step(3);assert(f.acknowledgements==1&&f.failures==1&&!f.playback.active());}
 std::cout<<"Reply output uses bounded files/ADPCM, waits for native drain, preserves text, handles backpressure, cancel and VM exit\n";
}
