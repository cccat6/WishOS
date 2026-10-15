#pragma once
#include "NativeOperation.h"
#include "LittleFsProvider.h"
#include <esp_littlefs.h>
namespace notesos {
class FlashVolumeService {
 Core&core_;Vfs&vfs_;fs::LittleFSFS&fs_;LittleFsProvider&provider_;bool&mounted_;const char*base_;const char*partition_;char method_[8]{};bool wasMounted_=false,ready_=false;
 struct Entry{void(*run)(void*);void*context;};
 static void task(void*p){auto e=*static_cast<Entry*>(p);delete static_cast<Entry*>(p);e.run(e.context);vTaskDelete(nullptr);}
 static const char*work(void*p,const std::atomic<bool>&cancel){auto&s=*static_cast<FlashVolumeService*>(p);if(cancel)return "cancelled";s.ready_=s.wasMounted_;
  if(!strcmp(s.method_,"format")){s.fs_.end();s.ready_=false;if(esp_littlefs_format(s.partition_)!=ESP_OK)return "format_failed";}
  if(!s.ready_)s.ready_=s.fs_.begin(false,s.base_,4,s.partition_);if(!s.ready_)return "mount_failed";
  return s.provider_.captureSpace();
 }
 NativeOperation operation_;
public:
 FlashVolumeService(Core&c,Vfs&v,fs::LittleFSFS&fs,LittleFsProvider&p,bool&mounted,const char*base,const char*partition):core_(c),vfs_(v),fs_(fs),provider_(p),mounted_(mounted),base_(base),partition_(partition),wasMounted_(mounted),ready_(mounted),operation_(c,"/dev/flash",{[](void(*run)(void*),void*ctx){auto entry=new(std::nothrow)Entry{run,ctx};if(!entry)return false;if(xTaskCreate(task,"wish-flash",8192,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},12288},this,work){provider_.spaceRequester(this,[](void*p)->const char*{JsonDocument out;auto error=static_cast<FlashVolumeService*>(p)->invoke("space",JsonObjectConst(),out.to<JsonObject>());return error?error:"resource_busy";});}

 // NativeOperation's executor receives its own object; use a stable generic task
 // trampoline without borrowing the foreground call stack.
 bool active()const{return operation_.active();}
 void poll(){operation_.poll();if(!operation_.active()){mounted_=ready_;provider_.background(false);}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"mount")||!strcmp(m,"format")||!strcmp(m,"space")){
   if(a.size())return "unknown_storage_argument";if(!strcmp(m,"format")&&core_.callerOwner())return "system_scope_required";if(operation_.active())return "operation_active";
   if(strcmp(m,"space")&&(vfs_.hasLeasesUnder(provider_.mount)||vfs_.pinned(provider_)))return "storage_in_use";if(!strcmp(m,"space")&&!mounted_)return "provider_unavailable";
   copy(method_,sizeof method_,m);wasMounted_=mounted_;ready_=mounted_;if(strcmp(m,"space"))mounted_=false;provider_.background(true);auto error=operation_.start(m,o);if(error){mounted_=wasMounted_;provider_.background(false);}return error;
  }
  if(!strcmp(m,"cancel"))return operation_.cancel(a);if(strcmp(m,"read"))return "unsupported_method";provider_.describe(o);operation_.read(o["operation"].to<JsonObject>());return nullptr;
 }
 void describe(JsonObject o)const{for(auto m:{"mount","format","space"})NativeOperation::describe(o,m,"/dev/flash");o["format_scope"]="explicit_system_request";}
};
}
