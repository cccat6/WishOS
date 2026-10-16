#pragma once
#include "NativeOperation.h"
#include "RetainedNativeWorker.h"
#include "Vfs.h"
#include "WireBytes.h"
#include "drivers/i2c/RegisterBus.h"
#include <esp_camera.h>
namespace notesos::drivers {
// Sensor setup and frame capture run on a native worker. A captured SDK frame
// remains leased until release; app replacement cancels or returns its own frame.
class EspCamera {
 Core&core_;I2cBus&bus_;camera_config_t config_;const char*(*power_)(bool);bool ready_=false,workerReady_=false;bool mirror_=false,flip_=false,colorbar_=false,psramDma_=true;camera_fb_t*frame_=nullptr;
 uint32_t owner_=0,frameId_=0,nextFrame_=1,internal_=0,external_=0;char method_[12]{};bool orphan_=false,closeRequested_=false;uint32_t lifecycleOwner_=0;
 struct Task{void(*run)(void*);void*context;};
 static void task(void*p){auto entry=*static_cast<Task*>(p);delete static_cast<Task*>(p);entry.run(entry.context);vTaskDelete(nullptr);}
 void releaseFrame(){if(frame_){esp_camera_fb_return(frame_);frame_=nullptr;}}
 const char*shutdown(){releaseFrame();if(workerReady_){auto error=esp_camera_deinit();if(error!=ESP_OK)return esp_err_to_name(error);workerReady_=false;}return power_(false);}
 static const char*work(void*p,const std::atomic<bool>&cancel){auto&s=*static_cast<EspCamera*>(p);if(cancel)return "cancelled";
  if(!strcmp(s.method_,"start")){
   if(auto e=s.power_(true))return e;vTaskDelay(pdMS_TO_TICKS(20));
   // SCCB reuses Wire's IDF master host, whose transfer arbiter serializes
   // both clients. Do not hold the outer RMW lock over sensor reset delays.
   // In the pinned SDK, the setter applies the mode before reporting an
   // uninitialized camera. Confirm that postcondition before the first init;
   // do not allocate internal DMA and then tear it down for a second init.
   auto mode=esp_camera_set_psram_mode(s.psramDma_);if(mode!=ESP_ERR_INVALID_STATE||esp_camera_get_psram_mode()!=s.psramDma_){s.power_(false);return "camera_dma_mode_not_applied";}
   auto e=esp_camera_init(&s.config_);if(e!=ESP_OK){s.power_(false);return esp_err_to_name(e);}s.workerReady_=true;
   auto sensor=esp_camera_sensor_get();if(!sensor||sensor->set_hmirror(sensor,s.mirror_)||sensor->set_vflip(sensor,s.flip_)||sensor->set_colorbar(sensor,s.colorbar_)){s.shutdown();return "camera_sensor_configuration_failed";}if(cancel){s.shutdown();return "cancelled";}return nullptr;
  }
  if(!strcmp(s.method_,"capture")){s.frame_=esp_camera_fb_get();if(!s.frame_)return "camera_capture_timeout";if(cancel){s.releaseFrame();return "cancelled";}return nullptr;}
  if(!strcmp(s.method_,"stop"))return s.shutdown();return "unsupported_method";
 }
 RetainedNativeWorker worker_;
 NativeOperation operation_;
 size_t internalWorkspace()const{return psramDma_?8192:98304;}
 void releaseBudget(){core_.resources().release(internal_);core_.resources().release(external_);internal_=external_=0;}
public:
 EspCamera(Core&c,I2cBus&bus,camera_config_t cfg,const char*(*power)(bool)):core_(c),bus_(bus),config_(cfg),power_(power),worker_(c.resources(),{[](void(*run)(void*),void*ctx){auto entry=new(std::nothrow)Task{run,ctx};if(!entry)return false;if(xTaskCreate(task,"wish-camera",12288,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},16384}),operation_(c,"/dev/camera",{nullptr,0,[](void*p,void(*run)(void*),void*ctx){return static_cast<EspCamera*>(p)->worker_.submit(run,ctx);}},this,work,[](void*p){auto&s=*static_cast<EspCamera*>(p);s.ready_=s.workerReady_;if(s.ready_){s.core_.resources().used(s.internal_,s.internalWorkspace());s.core_.resources().used(s.external_,(s.config_.frame_size==FRAMESIZE_VGA?614400:s.config_.frame_size==FRAMESIZE_QVGA?153600:38400)+65536);}if(!s.ready_){s.releaseBudget();s.worker_.shutdown();}if(s.orphan_){s.releaseFrame();s.orphan_=false;}}){c.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<EspCamera*>(p);if(s.lifecycleOwner_==owner)s.closeRequested_=true;if(s.owner_!=owner)return;if(s.operation_.active()){JsonDocument a;a["operation_id"]=0;s.orphan_=true;}else s.releaseFrame();});}
 ~EspCamera(){core_.removeOwnerHook(this);}
 void poll(){operation_.poll();if(closeRequested_&&!operation_.active()){releaseFrame();if(ready_){copy(method_,sizeof method_,"stop");JsonDocument result;if(!operation_.start("stop",result.to<JsonObject>()))closeRequested_=false;}else closeRequested_=false;}}
 bool active()const{return ready_||operation_.active();}
 void describe(JsonObject o)const{o["transport"]="DVP";o["model"]="GC0308";o["pixel_format"]="rgb565be";o["frame_sizes"]="qqvga,qvga,vga";o["configuration_scope"]="device";o["application_boundary"]="stopped";o["settings"]["frame_size"]="qqvga,qvga,vga";for(auto key:{"mirror","flip","colorbar","psram_dma"})o["settings"][key]="boolean";o["frame_retention"]="explicit release or owner exit";o["control_bus"]="shared native I2C master host arbiter";o["max_chunk_bytes"]=1024;o["internal_workspace_estimate_bytes"]=internalWorkspace();for(auto m:{"start","capture","stop"})NativeOperation::describe(o,m,"/dev/camera");}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(!strcmp(m,"read")){o["ready"]=ready_;o["configuration"]["frame_size"]=config_.frame_size==FRAMESIZE_VGA?"vga":config_.frame_size==FRAMESIZE_QVGA?"qvga":"qqvga";o["configuration"]["mirror"]=mirror_;o["configuration"]["flip"]=flip_;o["configuration"]["colorbar"]=colorbar_;o["configuration"]["psram_dma"]=psramDma_;o["operation_pending"]=operation_.active();if(!operation_.active()){o["frame_id"]=frame_?frameId_:0;if(frame_){o["width"]=frame_->width;o["height"]=frame_->height;o["bytes"]=frame_->len;o["pixel_format"]="rgb565be";}}operation_.read(o["operation"].to<JsonObject>());return nullptr;}
  if(!strcmp(m,"configure")){
   if(ready_||operation_.active())return "camera_in_use";if(auto e=argumentFields(a,"frame_size,mirror,flip,colorbar,psram_dma"))return e;
   auto size=config_.frame_size;bool mirror=a["mirror"]|mirror_,flip=a["flip"]|flip_,bar=a["colorbar"]|colorbar_,dma=a["psram_dma"]|psramDma_;
   for(auto key:{"mirror","flip","colorbar","psram_dma"})if(a.containsKey(key)&&!a[key].is<bool>())return "camera_setting_type";
   if(a.containsKey("frame_size")){if(a["frame_size"]=="qqvga")size=FRAMESIZE_QQVGA;else if(a["frame_size"]=="qvga")size=FRAMESIZE_QVGA;else if(a["frame_size"]=="vga")size=FRAMESIZE_VGA;else return "camera_frame_size";}
   config_.frame_size=size;mirror_=mirror;flip_=flip;colorbar_=bar;psramDma_=dma;return nullptr;
  }
  if(!strcmp(m,"cancel"))return operation_.cancel(a);if(operation_.active())return "operation_active";
  if(!strcmp(m,"read_chunk")||!strcmp(m,"release")){
   if(core_.callerOwner()&&core_.callerOwner()!=owner_)return "camera_frame_owned_by_other";if(!frame_||!a["frame_id"].is<uint32_t>()||a["frame_id"].as<uint32_t>()!=frameId_)return "camera_frame_not_found";
   if(!strcmp(m,"release")){if(auto e=argumentFields(a,"frame_id"))return e;releaseFrame();return nullptr;}
   if(auto e=argumentFields(a,"frame_id,offset,length"))return e;uint64_t offset=0;if(!parseOffset(a["offset"],offset))return "invalid_offset";if(a.containsKey("length")&&!a["length"].is<unsigned>())return "camera_chunk_size";unsigned n=a["length"]|1024u;if(!n||n>1024)return "camera_chunk_size";n=offset>=frame_->len?0:std::min<uint64_t>(n,frame_->len-offset);char text[2049];hex(n?frame_->buf+offset:frame_->buf,n,text);o["hex"]=text;sizeText(o,"next_offset",offset+n);return nullptr;
  }
  if(a.size())return "unknown_camera_argument";if(strcmp(m,"start")&&strcmp(m,"capture")&&strcmp(m,"stop"))return "unsupported_method";
  if(!strcmp(m,"start")){if(ready_)return "camera_already_started";internal_=core_.resources().acquire(internalWorkspace(),0,ResourceBudget::Region::Internal);if(!internal_)return "camera_memory_budget";external_=core_.resources().acquire((config_.frame_size==FRAMESIZE_VGA?614400:config_.frame_size==FRAMESIZE_QVGA?153600:38400)+65536,0,ResourceBudget::Region::External);if(!external_){releaseBudget();return "camera_memory_budget";}}
  else if(!ready_)return "camera_not_started";if(frame_)return "camera_frame_in_use";
  if(!strcmp(m,"start"))lifecycleOwner_=core_.callerOwner();copy(method_,sizeof method_,m);owner_=core_.callerOwner();if(!strcmp(m,"capture"))frameId_=nextFrame_++;auto e=operation_.start(m,o);if(e&&!ready_)releaseBudget();return e;
 }
};
}
