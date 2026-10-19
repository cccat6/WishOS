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
static unsigned captureStarts=0;static const char*begin(unsigned){++captureStarts;return nullptr;}static void release(){}
struct Observed {CompressedCapture*capture=nullptr;unsigned completed=0;bool durable=false;uint64_t missing=0;std::string file,error;};
static void observe(void*p,const char*id,const char*file,bool pending,bool durable,const char*error,uint64_t missing){auto&s=*static_cast<Observed*>(p);if(s.capture)s.capture->storageStatus(id,file,pending,durable,error,missing);if(file)s.file=file;s.durable=durable;s.error=error?error:"";s.missing=missing;if(!pending)++s.completed;}
static void settle(CompressedCapture&capture,Observed&state,unsigned count){auto deadline=now()+5000;while(state.completed<count&&now()<deadline){capture.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(state.completed>=count);if(worker.joinable())worker.join();}
static void releaseRam(CompressedCapture&capture){JsonDocument list,args,out;assert(!capture.invoke("list",JsonObjectConst(),list.to<JsonObject>()));for(auto item:list["recordings"].as<JsonArrayConst>())if(item["recording"]==false&&item["storage_pending"]==false){args["recording_id"]=item["recording_id"];assert(!capture.invoke("release",args.as<JsonObjectConst>(),out.to<JsonObject>()));}}
struct StorageHeap {
 struct alignas(max_align_t) Allocation{size_t size;};size_t used=0,allocations=0,failAt=SIZE_MAX;
 void*resize(void*p,size_t n){auto old=p?static_cast<Allocation*>(p)-1:nullptr;if(!n){if(old){used-=old->size;free(old);}return nullptr;}if(allocations++==failAt)return nullptr;auto next=static_cast<Allocation*>(realloc(old,n+sizeof(Allocation)));if(!next)return nullptr;used=used-(old?next->size:0)+n;next->size=n;return next+1;}
};
static StorageHeap storageInternal,storageExternal;static unsigned storageLaunches=0;static bool storageUsesExternal=false;
static void workspaceAdmission(bool psram){
 Core core;Vfs vfs;bool mounted=true;PosixFileProvider files("sd","/sd","sd",true,mounted,"/unused","","fat",512);vfs.add(files);Observed state;
 storageInternal={};storageExternal={};storageLaunches=0;storageUsesExternal=psram;
 core.resources().heap(ResourceBudget::Region::Internal,{[](){return size_t(1000000);},[](){return size_t(1000000)-storageInternal.used;},nullptr,[](void*p,size_t n){return storageInternal.resize(p,n);}});
 core.resources().heap(ResourceBudget::Region::External,{[](){return storageUsesExternal?size_t(1000000):size_t(0);},[](){return size_t(1000000)-storageExternal.used;},nullptr,[](void*p,size_t n){return storageExternal.resize(p,n);}});
 RecordingStorage disk(core,vfs,files,{[](void(*)(void*),void*){++storageLaunches;assert((storageUsesExternal?storageExternal.used:storageInternal.used)>4000);return false;},0},&state,observe);JsonDocument args;args["storage"]="history";args["storage_required"]=true;args["storage_buffer_blocks"]=1;
 auto&heap=psram?storageExternal:storageInternal;heap.failAt=1;assert(!strcmp(disk.begin("oom",0,16000,args.as<JsonObjectConst>()),"recording_storage_memory_budget"));assert(!storageLaunches&&!vfs.pinned(files)&&core.resources().promised()==0&&storageInternal.used==0&&storageExternal.used==0);
 heap.failAt=SIZE_MAX;assert(!strcmp(disk.begin("launch",0,16000,args.as<JsonObjectConst>()),"native_task_allocation_failed"));assert(storageLaunches==1&&!vfs.pinned(files)&&core.resources().promised()==0&&storageInternal.used==0&&storageExternal.used==0);assert((psram?storageInternal.allocations:storageExternal.allocations)==0);
}
int main(){workspaceAdmission(false);workspaceAdmission(true);Core core;core.clockSource(now);Vfs vfs;char root[]="/tmp/wishos-recording-XXXXXX";assert(mkdtemp(root));bool mounted=true;PosixFileProvider files("user","/sd","sd",true,mounted,root,"","fat",512);vfs.add(files);Observed observed;RecordingStorage disk(core,vfs,files,{launch,12288},&observed,observe);CompressedCapture mic(core,"test",begin,release);mic.storage(disk);observed.capture=&mic;core.add(Device::bind("/dev/mic","start,stop,read","",mic));RecordingArchive archive(vfs);JsonDocument a,o;
 // Directory policy is validated before capture, task launch or filesystem
 // effects. Paths normalize within this sink's selected provider.
 {JsonDocument description;disk.describe(description.to<JsonObject>());assert(description["settings"]["storage_directory"]["default"]=="/recordings"&&description["settings"]["storage_directory"]["base"]=="selected_provider_root");}
 for(const char*path:{"../escape","/a/../../escape","bad\\path","", "/this-directory-name-is-too-long-this-directory-name-is-too-long-this-directory-name-is-too-long-this-directory-name-is-too-long-this-directory-name-is-too-long"}){a.clear();a["storage"]="history";a["storage_directory"]=path;assert(mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!mic.active()&&!launched&&captureStarts==0&&std::filesystem::is_empty(root));}
 for(bool nul:{false,true}){a.clear();if(nul)a["storage_directory"]=JsonString("a\0b",3);else a["storage_directory"]=42;assert(mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!launched&&captureStarts==0&&std::filesystem::is_empty(root));}a.clear();
 // Default capture does no storage work. Optional missing media still permits
 // RAM capture; required storage refuses without leaving a recording active.
 a["max_ms"]=100;assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!launched);mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.poll();mounted=false;a["storage"]="history";a["storage_required"]=true;assert(!strcmp(mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()),"recording_storage_unavailable"));assert(!mic.active());a["storage_required"]=false;assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>())&&mic.active());mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.poll();assert(!launched&&!std::filesystem::exists(std::string(root)+"/recordings"));mounted=true;
 // Real files, independent compressed blocks, final index and random access.
 a.clear();a["storage"]="history";a["storage_required"]=true;a["storage_max_count"]=1;a["storage_on_limit"]="delete_oldest";a["storage_buffer_blocks"]=128;a["buffer_blocks"]=4;assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));auto id=o["recording_id"].as<std::string>();{auto deadline=now()+3000;auto index=std::string(root)+"/recordings/"+id+".adpcm.idx";while(!std::filesystem::exists(index)&&now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));assert(std::filesystem::exists(index));FileService api(vfs,&core);JsonDocument query,result;query["file"]="/sd";
  // A metadata lease permits the parent listing while the child is written;
  // the shared physical bus may still report a short, retryable collision.
  const char*listError=nullptr;deadline=now()+1000;do{result.clear();listError=api.invoke("list",query.as<JsonObjectConst>(),result.to<JsonObject>());if(!listError||strcmp(listError,"bus_busy"))break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}while(now()<deadline);assert(!listError);
  query["file"]="/sd/recordings/"+id+".adpcm";assert(!strcmp(api.invoke("read",query.as<JsonObjectConst>(),result.to<JsonObject>()),"file_in_use"));}int16_t pcm[160];for(unsigned i=0;i<160;++i)pcm[i]=1000;
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
 // A recording freezes its normalized directory before the asynchronous
 // writer starts. Another start/validation cannot retarget its pending tail.
 releaseRam(mic);auto outside=observed.file;FileInfo outsideInfo;const char*outsideRelative;vfs.resolve(outside.c_str(),outsideRelative);assert(!files.stat(outsideRelative,outsideInfo));std::string outsidePath=outsideRelative;
 allowWork=false;a.clear();a["storage"]="history";a["storage_required"]=true;a["storage_directory"]="/sets/transient/../A";a["storage_max_count"]=1;a["storage_on_limit"]="delete_oldest";
 assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));auto frozenId=o["recording_id"].as<std::string>();mic.process({pcm,160,16000,"M"});
 JsonDocument changed;changed.set(a);changed["storage_directory"]="/sets/B";assert(!disk.validate(changed.as<JsonObjectConst>()));assert(!strcmp(mic.invoke("start",changed.as<JsonObjectConst>(),o.to<JsonObject>()),"mic_busy"));
 mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});assert(!strcmp(mic.invoke("start",changed.as<JsonObjectConst>(),o.to<JsonObject>()),"recording_storage_busy"));
 before=observed.completed;allowWork=true;settle(mic,observed,before+1);assert(observed.durable&&observed.error.empty()&&observed.file=="/sd/sets/A/"+frozenId+".adpcm");auto inA=observed.file;
 assert(!std::filesystem::exists(std::string(root)+"/sets/B"));assert(!files.stat(outsidePath.c_str(),info));releaseRam(mic);
 // Count retention and deletion stay inside the session's directory, even
 // when another directory contains equally valid recording metadata.
 assert(!mic.invoke("start",changed.as<JsonObjectConst>(),o.to<JsonObject>()));mic.process({pcm,160,16000,"M"});mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});before=observed.completed;settle(mic,observed,before+1);assert(observed.durable&&observed.error.empty());auto inB=observed.file;releaseRam(mic);assert(vfs.lockRead(inB.c_str(),9001));
 assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));mic.process({pcm,160,16000,"M"});mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});before=observed.completed;settle(mic,observed,before+1);assert(observed.durable&&observed.error.empty());auto replacement=observed.file;releaseRam(mic);vfs.release(9001);
 assert(!strcmp(files.stat(inA.substr(3).c_str(),info),"not_found"));assert(!files.stat(inB.substr(3).c_str(),info)&&!files.stat(outsidePath.c_str(),info));
 // stop_storage limits reject an archive independently of RAM capture and
 // preserve the existing archive. Byte caps use the same selected root.
 a["storage_on_limit"]="stop_storage";assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));mic.process({pcm,160,16000,"M"});mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});before=observed.completed;settle(mic,observed,before+1);assert(observed.error=="recording_retention_limit"&&!vfs.pinned(files));assert(!files.stat(replacement.substr(3).c_str(),info));
 releaseRam(mic);a["storage_directory"]="/sets/bytes";a["storage_max_bytes"]="512";assert(!mic.invoke("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));mic.process({pcm,160,16000,"M"});mic.invoke("stop",JsonObjectConst(),o.to<JsonObject>());mic.process({pcm,160,16000,"M"});before=observed.completed;settle(mic,observed,before+1);assert(observed.error=="recording_space_limit"&&!vfs.pinned(files));
 assert(!files.stat(inB.substr(3).c_str(),info)&&!files.stat(outsidePath.c_str(),info));
 std::filesystem::remove_all(root);std::cout<<"Compressed RAM default, storage admission, native file/index finalization, random block access and owned retention passed\n";
}
