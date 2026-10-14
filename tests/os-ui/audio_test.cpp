#include "drivers/energy-activity/EnergyActivity.h"
#include "ScriptHost.h"
#include "audio/Speech.h"
#include "audio/Playback.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace notesos;using namespace notesos::audio;
static Core core;static unsigned captures=0,physicalStarts=0;static unsigned captureRate=16000;
static const char*acquire(unsigned rate,const char*roles="M"){if(strcmp(roles,"M"))return "capture_layout_unavailable";if(captures&&rate!=captureRate)return "capture_format_conflict";if(!captures){++physicalStarts;captureRate=rate;}++captures;return nullptr;}static void release(){assert(captures);--captures;}
struct Output final:OutputBackend {
 bool opened=false,drain=false,fail=false;unsigned opens=0;std::vector<int16_t>samples;
 void describe(JsonObject o)const override{o["adapter"]="host-fixture";o["full_duplex"]=true;}
 const char*validate(PcmFormat f)const override{return f.rate==16000&&f.channels==1?nullptr:"audio_format";}
 const char*open(PcmFormat)override{if(fail)return "fixture_start_failed";opened=true;++opens;return nullptr;}
 const char*write(const int16_t*p,size_t n,size_t&accepted)override{accepted=std::min<size_t>(n,2);samples.insert(samples.end(),p,p+accepted);return nullptr;}
 bool drained()override{return drain;}void close()override{opened=false;}
};
static Output output;static Playback speaker(core,output);static EnergyActivity energy;static SpeechBackend*engines[]={&energy};static SpeechService speech(core,"/dev/speech",engines,1,acquire,release);static ScriptHost*runtime;
int main(){
 core.add({"/dev/speaker","describe,read,start,write,finish,pause,resume,stop,volume","",[](const char*m,JsonObjectConst a,JsonObject o){return speaker.invoke(m,a,o);},[](JsonObject o){speaker.describe(o);},"read,describe"});
 core.add({"/dev/speech","read,describe,start,stop","",[](const char*m,JsonObjectConst a,JsonObject o){return speech.invoke(m,a,o);},[](JsonObject o){speech.describe(o);},"read,describe"});
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return core.describe(a["path"]|"",o);},nullptr,"describe"});
 JsonDocument a,o,empty;auto owner=core.acquireOwner();a["engine"]="energy-activity.v1";a["parameters"]["attack_ms"]=1;a["parameters"]["release_ms"]=1;
 // A recorder already holds the capture stream. Detection shares it, and does
 // not disconnect that recorder when it stops.
 assert(!acquire(16000));assert(!core.invoke("/dev/speech","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));assert(captures==2&&physicalStarts==1);
 int16_t loud[160];for(auto&v:loud)v=12000;speech.process({loud,160,16000,"M"});assert(!core.invoke("/dev/speech","stop",empty.to<JsonObject>(),o.to<JsonObject>(),owner));assert(captures==1);release();
 assert(!acquire(48000));assert(!strcmp(core.invoke("/dev/speech","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner),"capture_format_conflict"));assert(captures==1&&!speech.active());release();
 // The actual runtime application owns the meaning of the detection event.
 // Its chosen audio is a byte stream, not an embedded TTS/cloud conversation.
 Vfs vfs;ScriptHost app(core,vfs);runtime=&app;core.setEventObserver([](const Event&e){return runtime->event(e);});
 a.clear();a["memory_bytes"]=65536;a["source"]=R"lua(
 function on_start()
  notes.on('/dev/speech','activity_started',function()
   local p=notes.request('/dev/speaker','start',{sample_rate_hz=16000,channels=1,buffer_blocks=2},function(e) assert(e.event=='completed');print('speaker drained') end)
   assert(p.ok);assert(notes.call('/dev/speaker','write',{playback_id=p.result.playback_id,sequence=0,hex='e80318fc00000000'}).ok)
   assert(notes.call('/dev/speaker','finish',{playback_id=p.result.playback_id}).ok)
  end)
  assert(notes.call('/dev/speech','start',{engine='energy-activity.v1',parameters={attack_ms=1}}).ok)
 end)lua";
 assert(!app.invoke("load",a.as<JsonObjectConst>(),o.to<JsonObject>()));uint32_t cursor=core.sequence();speech.process({loud,160,16000,"M"});core.dispatch(cursor);app.tick(1);assert(speaker.active());
 for(unsigned i=0;i<6;++i)speaker.pump();assert(output.samples.size()==4&&output.samples[0]==1000&&output.samples[1]==-1000);assert(speaker.active()); // writing/DMA acceptance is not drained completion
 output.drain=true;speaker.pump();core.dispatch(cursor);app.tick(2);assert(!speaker.active()&&!output.opened);
 assert(!app.invoke("stop",empty.as<JsonObjectConst>(),o.to<JsonObject>()));assert(captures==0&&!speech.active());assert(core.resources().promised()==0);
 // Invalid format, task failure and budget rejection preserve resource state.
 a.clear();a["sample_rate_hz"]=12345;assert(core.invoke("/dev/speaker","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));assert(!speaker.active()&&core.resources().promised()==0);
 a["sample_rate_hz"]=16000;output.fail=true;assert(!core.request("/dev/speaker","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));auto observation=o["completion_id"].as<uint32_t>();speaker.pump();assert(!speaker.active());JsonDocument result;assert(!core.completions().read(observation,owner,result.to<JsonObject>()));assert(result["event"]=="failed");assert(!core.completions().release(observation,owner));assert(core.resources().promised()==0);output.fail=false;
 JsonDocument budget;budget["memory_limit_bytes"]=100;assert(!core.resources().configure(budget.as<JsonObjectConst>()));assert(core.invoke("/dev/speaker","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));assert(core.resources().promised()==0);budget["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(budget.as<JsonObjectConst>()));
 a["buffer_blocks"]=1;assert(!core.invoke("/dev/speaker","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));JsonDocument packet;packet["playback_id"]=o["playback_id"];packet["sequence"]=0;packet["hex"]="01000200";assert(!core.invoke("/dev/speaker","write",packet.as<JsonObjectConst>(),o.to<JsonObject>(),owner));assert(!core.invoke("/dev/speaker","write",packet.as<JsonObjectConst>(),o.to<JsonObject>(),owner)&&o["duplicate"]==true);packet["sequence"]=1;assert(!strcmp(core.invoke("/dev/speaker","write",packet.as<JsonObjectConst>(),o.to<JsonObject>(),owner),"audio_would_block"));
 speaker.pump();packet.remove("sequence");packet.remove("hex");assert(!core.invoke("/dev/speaker","pause",packet.as<JsonObjectConst>(),o.to<JsonObject>(),owner));size_t before=output.samples.size();speaker.pump();assert(output.samples.size()==before);core.releaseOwner(owner);speaker.pump();assert(!speaker.active()&&core.resources().promised()==0);
 std::cout<<"Audio shared input, detector ownership, runtime-defined wake actions, streamed playback, drain completion, failure rollback, duplicate blocks, backpressure, pause and owner cleanup passed\n";
}
