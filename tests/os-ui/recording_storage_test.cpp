#include "audio/CompressedCapture.h"
#include "audio/RecordingArchive.h"
#include "FileService.h"
#include "drivers/esp32-services/PosixFileProvider.h"
#include <cassert>
#include <thread>
#include <filesystem>
#include <iostream>
using namespace notesos;
using namespace notesos::audio;
static std::atomic<bool>allowWork{true};
static std::thread worker;static std::atomic<bool> launched{false};
static uint64_t now(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
static bool launch(void(*run)(void*),void*p){assert(!worker.joinable());launched=true;worker=std::thread([=]{while(!allowWork.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));run(p);});return true;}
static const char*begin(unsigned){return nullptr;}static void release(){}
struct Observed {CompressedCapture*capture=nullptr;unsigned completed=0;bool durable=false;uint64_t missing=0;std::string file,error;};
static void observe(void*p,const char*id,const char*file,bool pending,bool durable,const char*error,uint64_t missing){auto&s=*static_cast<Observed*>(p);if(s.capture)s.capture->storageStatus(id,file,pending,durable,error,missing);if(file)s.file=file;s.durable=durable;s.error=error?error:"";s.missing=missing;if(!pending)++s.completed;}
static void settle(CompressedCapture&capture,Observed&state,unsigned count){auto deadline=now()+5000;while(state.completed<count&&now()<deadline){capture.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(state.completed>=count);if(worker.joinable())worker.join();}
int main(){Core core;core.clockSource(now);Vfs vfs;char root[]="/tmp/wishos-recording-XXXXXX";assert(mkdtemp(root));bool mounted=true;PosixFileProvider files("user","/sd","sd",true,mounted,root,"","fat",512);vfs.add(files);Observed observed;RecordingStorage disk(core,vfs,files,{launch,12288},&observed,observe);CompressedCapture mic(core,"test",begin,release);mic.storage(disk);observed.capture=&mic;core.add(Device::bind("/dev/mic","start,stop,read","",mic));RecordingArchive archive(vfs);JsonDocument a,o;
 // Default capture does no storage work. Optional missing media still permits
 // RAM capture; required storage refuses without leaving a recording active.
 a["max_ms"]=100;assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!launched);mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.poll();mounted=false;a["storage"]="history";a["storage_required"]=true;assert(!strcmp(mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()),"recording_storage_unavailable"));assert(!mic.active());a["storage_required"]=false;assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>())&&mic.active());mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.poll();assert(!launched&&!std::filesystem::exists(std::string(root)+"/recordings"));mounted=true;
 // Real files, independent compressed blocks, final index and random access.
 a.clear();a["storage"]="history";a["storage_required"]=true;a["storage_max_count"]=1;a["storage_on_limit"]="delete_oldest";a["storage_buffer_blocks"]=128;a["buffer_blocks"]=4;assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));auto id=o["recording_id"].as<std::string>();{auto deadline=now()+3000;auto index=std::string(root)+"/recordings/"+id+".adpcm.idx";while(!std::filesystem::exists(index)&&now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(std::filesystem::exists(index));FileService api(vfs,&core);JsonDocument query,result;query["file"]="/sd";assert(!api.invoke("list",query.as<JsonObjectConst>(),result.to<JsonObject>()));query["file"]="/sd/recordings/"+id+".adpcm";assert(!strcmp(api.invoke("read",query.as<JsonObjectConst>(),result.to<JsonObject>()),"file_in_use"));}int16_t pcm[160];for(unsigned i=0;i<160;++i)pcm[i]=1000;
 for(unsigned i=0;i<32;++i)mic.process({pcm,160,16000,"M"});mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});auto before=observed.completed;settle(mic,observed,before+1);assert(observed.durable&&observed.error.empty()&&!observed.missing);auto first=observed.file;
 JsonDocument read;read["file"]=first;read["cursor"]=31;read["limit"]=1;if(auto e=archive.invoke("read_chunk",read.as<JsonObjectConst>(),o.to<JsonObject>())){std::cerr<<"archive: "<<e<<" file="<<first<<"\n";assert(false);}assert(o["chunks"].size()==1&&o["chunks"][0]["sequence"]==31&&o["more"]==false&&o["metadata"]["capture"]=="finalized");assert(!vfs.pinned(files));
 // A later explicit history policy evicts only the prior owned recording.
 assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));mic.process({pcm,160,16000,"M"});mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});before=observed.completed;settle(mic,observed,before+1);FileInfo info;const char*relative;vfs.resolve(first.c_str(),relative);assert(!strcmp(files.stat(relative,info),"not_found"));assert(observed.file!=first&&observed.durable);
 // Deterministic backpressure: the storage worker is delayed while capture
 // continues. Loss is counted, and configured stop_capture terminates only
 // this recording; the eventual archive stays explicitly incomplete.
 allowWork=false;a.clear();a["storage"]="history";a["storage_buffer_blocks"]=1;a["storage_on_failure"]="stop_capture";assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));mic.process({pcm,160,16000,"M"});mic.process({pcm,160,16000,"M"});assert(!mic.active());before=observed.completed;allowWork=true;settle(mic,observed,before+1);assert(observed.missing==160&&observed.durable);read.clear();read["file"]=observed.file;assert(!archive.invoke("read",read.as<JsonObjectConst>(),o.to<JsonObject>()));assert(o["metadata"]["capture"]=="incomplete"&&o["metadata"]["capture_failed"]==true);
 // The microphone follows its VM, while its already accepted storage tail
 // finalizes independently. Exiting during preparation must not cancel history.
 allowWork=false;a.clear();a["storage"]="history";auto owner=core.acquireOwner();assert(!core.invoke("/dev/mic","start",a.as<JsonObjectConst>(),o.to<JsonObject>(),owner));mic.process({pcm,160,16000,"M"});core.releaseOwner(owner);mic.process({pcm,160,16000,"M"});before=observed.completed;allowWork=true;settle(mic,observed,before+1);assert(observed.durable&&observed.error.empty()&&!vfs.pinned(files));read.clear();read["file"]=observed.file;assert(!archive.invoke("read",read.as<JsonObjectConst>(),o.to<JsonObject>())&&o["metadata"]["capture"]=="finalized");
 // A required writer that cannot acquire memory preserves capture admission.
 a.clear();a["storage"]="history";a["storage_required"]=true;a["storage_buffer_blocks"]=UINT32_MAX;assert(mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!mic.active()&&!vfs.pinned(files));
 std::filesystem::remove_all(root);std::cout<<"Compressed RAM default, storage admission, native file/index finalization, random block access and owned retention passed\n";
}
