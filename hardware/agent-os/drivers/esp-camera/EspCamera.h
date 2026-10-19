#pragma once
#include <atomic>
#include "NativeOperation.h"
#include "RetainedNativeWorker.h"
#include "Vfs.h"
#include "WireBytes.h"
#include "drivers/i2c/RegisterBus.h"
#include <sdkconfig.h>
#include <esp_camera.h>
#include <esp_timer.h>
#include "EspCameraClock.h"
namespace notesos::drivers {
// Sensor setup and frame capture run on a native worker. A captured SDK frame
// remains leased until release; app replacement cancels or returns its own frame.
struct CameraControls {bool exposure,whiteBalance,gain;};
struct CameraStartup {uint32_t frames,milliseconds;};
class EspCamera {
 Core&core_;Vfs&vfs_;I2cBus&bus_;camera_config_t config_;const char*model_;const char*(*power_)(bool);bool ready_=false;std::atomic<bool>workerReady_{false};bool mirror_=false,flip_=false,colorbar_=false,psramDma_;camera_fb_t*frame_=nullptr;
 EspCameraClock clock_;CameraControls controls_;CameraStartup startup_;bool autoExposure_=true,autoWhiteBalance_=true,autoGain_=true;std::atomic<uint32_t>discarded_{0},detectedPid_{0};uint64_t warmupElapsed_=0;
 uint32_t owner_=0,frameId_=0,nextFrame_=1,internal_=0,frames_=0;char method_[12]{};bool orphan_=false,closeRequested_=false;uint32_t lifecycleOwner_=0;
 struct Task{void(*run)(void*);void*context;};
 static void task(void*p){auto entry=*static_cast<Task*>(p);delete static_cast<Task*>(p);entry.run(entry.context);vTaskDelete(nullptr);}
 void releaseFrame(){if(frame_){esp_camera_fb_return(frame_);frame_=nullptr;}}
 const char*shutdown(){releaseFrame();if(workerReady_){auto error=esp_camera_deinit();if(error!=ESP_OK)return esp_err_to_name(error);workerReady_=false;clock_.release();}return power_(false);}
 const char*sensorSettings(const std::atomic<bool>&cancel){
  while(!cancel){
   {I2cBus::Transaction lease(bus_);if(lease.held){auto sensor=esp_camera_sensor_get();if(!sensor)return "camera_sensor_unavailable";detectedPid_=sensor->id.PID;
    if(!sensor->set_hmirror||!sensor->set_vflip||!sensor->set_colorbar||sensor->set_hmirror(sensor,mirror_)||sensor->set_vflip(sensor,flip_)||sensor->set_colorbar(sensor,colorbar_))return "camera_sensor_configuration_failed";
    if(controls_.exposure&&(!sensor->set_exposure_ctrl||sensor->set_exposure_ctrl(sensor,autoExposure_)))return "camera_auto_exposure_failed";
    if(controls_.whiteBalance&&(!sensor->set_whitebal||sensor->set_whitebal(sensor,autoWhiteBalance_)))return "camera_auto_white_balance_failed";
    if(controls_.gain&&(!sensor->set_gain_ctrl||sensor->set_gain_ctrl(sensor,autoGain_)))return "camera_auto_gain_failed";
    return nullptr;
   }}vTaskDelay(1);
  }return "cancelled";
 }
 const char*warmup(const std::atomic<bool>&cancel){
  const uint64_t began=uint64_t(esp_timer_get_time())/1000;discarded_=0;warmupElapsed_=0;
  while(discarded_.load()<startup_.frames||(uint64_t(esp_timer_get_time())/1000-began)<startup_.milliseconds){
   if(cancel)return "cancelled";
   auto frame=esp_camera_fb_get();if(!frame)return "camera_warmup_timeout";
   const bool valid=frame->format==PIXFORMAT_RGB565&&frame->len==size_t(frame->width)*frame->height*2;
   esp_camera_fb_return(frame);if(!valid)return "camera_frame_format";
   ++discarded_;warmupElapsed_=(uint64_t(esp_timer_get_time())/1000-began);if(cancel)return "cancelled";
   vTaskDelay(1);
  }warmupElapsed_=(uint64_t(esp_timer_get_time())/1000-began);return nullptr;
 }
 static const char*work(void*p,const std::atomic<bool>&cancel){auto&s=*static_cast<EspCamera*>(p);if(cancel)return "cancelled";
  if(!strcmp(s.method_,"start")){
   s.discarded_=0;s.warmupElapsed_=0;
   if(auto e=s.power_(true))return e;vTaskDelay(pdMS_TO_TICKS(20));
   // SCCB reuses Wire's IDF master host, whose transfer arbiter serializes
   // both clients. Do not hold the outer RMW lock over sensor reset delays.
   // In the pinned SDK, the setter applies the mode before reporting an
   // uninitialized camera. Confirm that postcondition before the first init;
   // do not allocate internal DMA and then tear it down for a second init.
   auto mode=esp_camera_set_psram_mode(s.psramDma_);if(mode!=ESP_ERR_INVALID_STATE||esp_camera_get_psram_mode()!=s.psramDma_){s.power_(false);return "camera_dma_mode_not_applied";}
   auto e=esp_camera_init(&s.config_);if(e!=ESP_OK){s.power_(false);return esp_err_to_name(e);}s.workerReady_=true;
   if(auto error=s.clock_.apply(s.config_.xclk_freq_hz)){s.shutdown();return error;}
   if(auto error=s.sensorSettings(cancel)){s.shutdown();return error;}if(auto error=s.warmup(cancel)){s.shutdown();return error;}if(cancel){s.shutdown();return "cancelled";}return nullptr;
  }
  if(!strcmp(s.method_,"capture")){s.frame_=esp_camera_fb_get();if(!s.frame_)return "camera_capture_timeout";if(cancel){s.releaseFrame();return "cancelled";}if(s.frame_->format!=PIXFORMAT_RGB565||s.frame_->len!=size_t(s.frame_->width)*s.frame_->height*2){s.releaseFrame();return "camera_frame_format";}return nullptr;}
  if(!strcmp(s.method_,"stop"))return s.shutdown();return "unsupported_method";
 }
 RetainedNativeWorker worker_;
 NativeOperation operation_;
 // The fixed S3 SDK's non-direct path adds one internal DMA buffer;
 // descriptors/control/SDK task remain in the existing 8 KiB workspace.
 // For supported RGB565 sizes, its eight 3840-byte nodes use 96 bytes
 // of descriptors, less than the direct QVGA path's 480 bytes.
 size_t internalWorkspace()const{return 8192+(psramDma_?0:CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX);}
 ResourceBudget::Region frameRegion()const{return config_.fb_location==CAMERA_FB_IN_DRAM?ResourceBudget::Region::Internal:ResourceBudget::Region::External;}
 size_t frameStorage()const{return (size_t(config_.frame_size==FRAMESIZE_VGA?614400:config_.frame_size==FRAMESIZE_QVGA?153600:38400)+65536)*config_.fb_count;}
 const char*configurationError(bool dma,framesize_t size)const{
  if(auto error=EspCameraClock::validate(config_.xclk_freq_hz))return error;
  if(config_.pixel_format!=PIXFORMAT_RGB565)return "camera_pixel_format";
  if(size!=FRAMESIZE_QQVGA&&size!=FRAMESIZE_QVGA&&size!=FRAMESIZE_VGA)return "camera_frame_size";
  if(config_.fb_location!=CAMERA_FB_IN_DRAM&&config_.fb_location!=CAMERA_FB_IN_PSRAM)return "camera_frame_location";
  if(!config_.fb_count||config_.fb_count>SIZE_MAX/(size_t(size==FRAMESIZE_VGA?614400:size==FRAMESIZE_QVGA?153600:38400)+65536))return "camera_frame_count";
  if(dma&&config_.fb_location!=CAMERA_FB_IN_PSRAM)return "camera_psram_dma_requires_psram_frames";
  return nullptr;
 }
 void releaseBudget(){core_.resources().release(internal_);core_.resources().release(frames_);internal_=frames_=0;}
public:
 EspCamera(Core&c,Vfs&vfs,I2cBus&bus,camera_config_t cfg,const char*(*power)(bool),const char*model=nullptr,CameraControls controls={false,false,false},CameraStartup startup={0,0}):core_(c),vfs_(vfs),bus_(bus),config_(cfg),model_(model),power_(power),psramDma_(cfg.fb_location==CAMERA_FB_IN_PSRAM),controls_(controls),startup_(startup),worker_(c.resources(),{[](void(*run)(void*),void*ctx){auto entry=new(std::nothrow)Task{run,ctx};if(!entry)return false;if(xTaskCreate(task,"wish-camera",12288,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},16384}),operation_(c,"/dev/camera",{nullptr,0,[](void*p,void(*run)(void*),void*ctx){return static_cast<EspCamera*>(p)->worker_.submit(run,ctx);}},this,work,[](void*p){auto&s=*static_cast<EspCamera*>(p);if(!strcmp(s.method_,"start")){JsonDocument result;s.operation_.read(result.to<JsonObject>());s.ready_=s.workerReady_&&!(result["error"]|"")[0];}else if(!strcmp(s.method_,"stop"))s.ready_=false;if(s.workerReady_){s.core_.resources().used(s.internal_,s.internalWorkspace());s.core_.resources().used(s.frames_,s.frameStorage());}if(!s.workerReady_){s.releaseBudget();s.worker_.shutdown();}if(s.orphan_){s.releaseFrame();s.orphan_=false;}}){c.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*static_cast<EspCamera*>(p);if(s.lifecycleOwner_==owner){s.closeRequested_=true;s.lifecycleOwner_=0;}if(s.owner_!=owner)return;if(s.operation_.active()){s.orphan_=true;}else s.releaseFrame();});}
 ~EspCamera(){core_.removeOwnerHook(this);}
 void poll(){operation_.poll();if(closeRequested_&&!operation_.active()){releaseFrame();if(workerReady_){copy(method_,sizeof method_,"stop");JsonDocument result;if(!operation_.start("stop",result.to<JsonObject>()))closeRequested_=false;}else closeRequested_=false;}}
 bool active()const{return workerReady_||operation_.active();}
 void describe(JsonObject o)const{o["transport"]="DVP";if(model_&&*model_)o["model"]=model_;o["pixel_format"]="rgb565be";o["frame_sizes"]="qqvga,qvga,vga";o["configuration_scope"]="device";o["application_boundary"]="stopped";o["settings"]["frame_size"]="qqvga,qvga,vga";for(auto key:{"mirror","flip","colorbar","psram_dma"})o["settings"][key]="boolean";if(controls_.exposure)o["settings"]["auto_exposure"]="boolean";if(controls_.whiteBalance)o["settings"]["auto_white_balance"]="boolean";if(controls_.gain)o["settings"]["auto_gain"]="boolean";o["settings"]["warmup_frames"]="uint32";o["settings"]["warmup_ms"]="uint32";o["startup_warmup"]="discard_until_both_configured_frame_count_and_elapsed_time;not_optical_convergence_detection";o["frame_timestamp"]="captured_at_us_from_sdk_monotonic_boot_clock";o["frame_retention"]="explicit release or owner exit";o["control_bus"]="shared native I2C master host arbiter";o["max_chunk_bytes"]=1024;o["read_chunk_pixel_formats"]="rgb565be,rgb565le";o["copy_frame_destination"]="existing_volatile_file_append";o["copy_frame_pixel_formats"]="rgb565be,rgb565le";o["internal_workspace_estimate_bytes"]=internalWorkspace();if constexpr(EspCameraClock::exactSupported())o["clock_selection"]="exact_integer_or_fractional_divider;compiled_xclk_frequency";o["framebuffer_region"]=ResourceBudget::name(frameRegion());o["framebuffer_count"]=config_.fb_count;o["framebuffer_storage_estimate_bytes"]=frameStorage();for(auto m:{"start","capture","stop"})NativeOperation::describe(o,m,"/dev/camera");}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(!strcmp(m,"read")){o["ready"]=ready_;o["lifecycle_owner"]=lifecycleOwner_;if(auto id=detectedPid_.load())o["detected_sensor_pid"]=id;o["initialized"]=workerReady_.load();o["configuration"]["frame_size"]=config_.frame_size==FRAMESIZE_VGA?"vga":config_.frame_size==FRAMESIZE_QVGA?"qvga":"qqvga";o["configuration"]["mirror"]=mirror_;o["configuration"]["flip"]=flip_;o["configuration"]["colorbar"]=colorbar_;o["configuration"]["psram_dma"]=psramDma_;o["configuration"]["framebuffer_region"]=ResourceBudget::name(frameRegion());o["configuration"]["framebuffer_count"]=config_.fb_count;o["configuration"]["requested_xclk_hz"]=config_.xclk_freq_hz;o["configuration"]["warmup_frames"]=startup_.frames;o["configuration"]["warmup_ms"]=startup_.milliseconds;if(controls_.exposure)o["configuration"]["auto_exposure"]=autoExposure_;if(controls_.whiteBalance)o["configuration"]["auto_white_balance"]=autoWhiteBalance_;if(controls_.gain)o["configuration"]["auto_gain"]=autoGain_;o["startup"]["discarded_frames"]=discarded_.load();if(!operation_.active())sizeText(o["startup"].as<JsonObject>(),"elapsed_ms",warmupElapsed_);o["operation_pending"]=operation_.active();if(!operation_.active()){if constexpr(EspCameraClock::exactSupported())clock_.read(o["clock"].to<JsonObject>());o["frame_id"]=frame_?frameId_:0;o["frame_owner"]=frame_?owner_:0;if(frame_){o["width"]=frame_->width;o["height"]=frame_->height;o["bytes"]=frame_->len;o["pixel_format"]="rgb565be";sizeText(o,"captured_at_us",uint64_t(frame_->timestamp.tv_sec)*1000000+frame_->timestamp.tv_usec);}}operation_.read(o["operation"].to<JsonObject>());return nullptr;}
  if(!strcmp(m,"configure")){
   if(workerReady_||operation_.active())return "camera_in_use";if(auto e=argumentFields(a,"frame_size,mirror,flip,colorbar,psram_dma,auto_exposure,auto_white_balance,auto_gain,warmup_frames,warmup_ms"))return e;
   auto size=config_.frame_size;bool mirror=a["mirror"]|mirror_,flip=a["flip"]|flip_,bar=a["colorbar"]|colorbar_,dma=a["psram_dma"]|psramDma_;
   for(auto key:{"mirror","flip","colorbar","psram_dma","auto_exposure","auto_white_balance","auto_gain"})if(a.containsKey(key)&&!a[key].is<bool>())return "camera_setting_type";for(auto key:{"warmup_frames","warmup_ms"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "camera_setting_type";if((a.containsKey("auto_exposure")&&!controls_.exposure)||(a.containsKey("auto_white_balance")&&!controls_.whiteBalance)||(a.containsKey("auto_gain")&&!controls_.gain))return "camera_control_unavailable";
   if(a.containsKey("frame_size")){if(a["frame_size"]=="qqvga")size=FRAMESIZE_QQVGA;else if(a["frame_size"]=="qvga")size=FRAMESIZE_QVGA;else if(a["frame_size"]=="vga")size=FRAMESIZE_VGA;else return "camera_frame_size";}
   if(auto error=configurationError(dma,size))return error;
   config_.frame_size=size;mirror_=mirror;flip_=flip;colorbar_=bar;psramDma_=dma;autoExposure_=a["auto_exposure"]|autoExposure_;autoWhiteBalance_=a["auto_white_balance"]|autoWhiteBalance_;autoGain_=a["auto_gain"]|autoGain_;startup_.frames=a["warmup_frames"]|startup_.frames;startup_.milliseconds=a["warmup_ms"]|startup_.milliseconds;return nullptr;
  }
  if(!strcmp(m,"cancel"))return operation_.cancel(a);if(operation_.active())return "operation_active";
  if(!strcmp(m,"read_chunk")||!strcmp(m,"release")||!strcmp(m,"copy_frame")){
   if(core_.callerOwner()&&core_.callerOwner()!=owner_)return "camera_frame_owned_by_other";if(!frame_||!a["frame_id"].is<uint32_t>()||a["frame_id"].as<uint32_t>()!=frameId_)return "camera_frame_not_found";
   if(!strcmp(m,"release")){if(auto e=argumentFields(a,"frame_id"))return e;releaseFrame();return nullptr;}
   if(!strcmp(m,"copy_frame")){
    if(auto e=argumentFields(a,"frame_id,file,offset,pixel_format"))return e;
    if(!a["file"].is<const char*>())return "file_path_required";char full[256];if(!normalizePath("/",a["file"],full,sizeof full))return "invalid_path";const char*relative;auto target=vfs_.resolve(full,relative);if(!target||!target->available())return "provider_unavailable";if(target->persistent)return "volatile_destination_required";if(!target->writable)return "read_only";
    uint64_t at=0;if(!parseOffset(a["offset"],at)||at>UINT64_MAX-frame_->len)return "invalid_offset";const char*format=a["pixel_format"]|"rgb565be";if(strcmp(format,"rgb565be")&&strcmp(format,"rgb565le"))return "camera_pixel_format";if(!strcmp(format,"rgb565le")&&(frame_->len&1))return "camera_pixel_alignment";
    Vfs::Access lease(vfs_,true);if(auto e=lease.add(full,target->caseInsensitive()))return e;FileInfo info;if(auto e=target->stat(relative,info))return e;if(info.directory||info.size!=at)return "frame_destination_not_append";
    // Allocate/validate the complete volatile destination before copying. The
    // bounded native loop avoids a hex/JSON/Lua round-trip for every 1 KiB.
    if(auto e=target->resize(relative,at+frame_->len))return e;bool swapped=!strcmp(format,"rgb565le");uint8_t bytes[1024];size_t copied=0;
    while(copied<frame_->len){size_t count=std::min(sizeof bytes,frame_->len-copied);for(size_t i=0;i<count;++i)bytes[i]=frame_->buf[copied+(swapped?(i^1u):i)];size_t wrote=0;if(auto e=target->write(relative,at+copied,bytes,count,false,wrote))return e;if(wrote!=count)return "frame_copy_short_write";copied+=count;}
    o["written"]=copied;sizeText(o,"next_offset",at+copied);o["pixel_format"]=format;return nullptr;
   }
   if(auto e=argumentFields(a,"frame_id,offset,length,pixel_format"))return e;uint64_t offset=0;if(!parseOffset(a["offset"],offset))return "invalid_offset";if(a.containsKey("length")&&!a["length"].is<unsigned>())return "camera_chunk_size";unsigned n=a["length"]|1024u;if(!n||n>1024)return "camera_chunk_size";n=offset>=frame_->len?0:std::min<uint64_t>(n,frame_->len-offset);const char*format=a["pixel_format"]|"rgb565be";if(strcmp(format,"rgb565be")&&strcmp(format,"rgb565le"))return "camera_pixel_format";if(!strcmp(format,"rgb565le")&&((offset|n)&1))return "camera_pixel_alignment";char text[2049];if(!strcmp(format,"rgb565le")){static constexpr char digits[]="0123456789abcdef";for(unsigned i=0;i<n;++i){auto byte=frame_->buf[offset+(i^1u)];text[2*i]=digits[byte>>4];text[2*i+1]=digits[byte&15];}text[2*n]=0;}else hex(n?frame_->buf+offset:frame_->buf,n,text);o["hex"]=text;o["pixel_format"]=format;sizeText(o,"next_offset",offset+n);return nullptr;
  }
  if(a.size())return "unknown_camera_argument";if(closeRequested_&&workerReady_&&strcmp(m,"stop"))return "camera_stopping";if(strcmp(m,"start")&&strcmp(m,"capture")&&strcmp(m,"stop"))return "unsupported_method";if(workerReady_&&lifecycleOwner_&&core_.callerOwner()&&core_.callerOwner()!=lifecycleOwner_)return "camera_owned_by_other";
  if(!strcmp(m,"start")){if(closeRequested_&&!workerReady_)closeRequested_=false;if(workerReady_)return ready_?"camera_already_started":"camera_cleanup_required";if(auto error=configurationError(psramDma_,config_.frame_size))return error;internal_=core_.resources().acquire(internalWorkspace(),0,ResourceBudget::Region::Internal);if(!internal_)return "camera_memory_budget";frames_=core_.resources().acquire(frameStorage(),0,frameRegion());if(!frames_){releaseBudget();return "camera_memory_budget";}}
  else if(!workerReady_||(!strcmp(m,"capture")&&!ready_))return "camera_not_started";if(frame_)return "camera_frame_in_use";
  if(!strcmp(m,"start"))lifecycleOwner_=core_.callerOwner();copy(method_,sizeof method_,m);owner_=core_.callerOwner();if(!strcmp(m,"capture"))frameId_=nextFrame_++;auto e=operation_.start(m,o);if(e&&!workerReady_)releaseBudget();return e;
 }
};
}
