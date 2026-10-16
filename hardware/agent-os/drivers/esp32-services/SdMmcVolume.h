#pragma once
#include "NativeOperation.h"
#include "PosixFileProvider.h"
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <diskio_impl.h>
#include <diskio_sdmmc.h>
#include <ff.h>
#include <Names.h>
namespace notesos {
// SDMMC link discovery is independent of FAT admission. No card sector is read
// or written to choose a clock. The SDK owns command timing; this mutex owns all
// access to that host, including native-worker mount/scan/recovery.
class SdMmcVolume {
 Core&core_;Vfs&vfs_;std::recursive_mutex bus_;bool mounted_=false,workerMounted_=false,host_=false,linked_=false;int clk_,cmd_,d0_;unsigned maxHz_,fixedHz_=0,actualHz_=0;bool readOnly_=false;
 sdmmc_card_t card_{};FATFS*fat_=nullptr;BYTE drive_=0xff;char driveName_[4]{},method_[12]{};const char*lastError_=nullptr;
 class Provider:public PosixFileProvider {
 public:Provider(bool&m,std::recursive_mutex&b):PosixFileProvider("user","/storage/user","sd",true,m,"/wish-sd","/dev/sd","fat",512,&b){}
  bool caseInsensitive()const override{return true;}
  bool sameName(const char*a,const char*b)const override{const char*ae=a+strlen(a),*be=b+strlen(b);while(a<ae&&b<be){uint32_t x,y;if(!esp32_nfs::name_detail::next(a,ae,x)||!esp32_nfs::name_detail::next(b,be,y))return false;if(ff_wtoupper(x)!=ff_wtoupper(y))return false;}return a==ae&&b==be;}
 }provider_;
 struct Entry{void(*run)(void*);void*context;};
 static void task(void*p){auto entry=*static_cast<Entry*>(p);delete static_cast<Entry*>(p);entry.run(entry.context);vTaskDelete(nullptr);}
 void unmount(){if(fat_){f_mount(nullptr,driveName_,0);esp_vfs_fat_unregister_path("/wish-sd");ff_diskio_unregister(drive_);fat_=nullptr;drive_=0xff;}workerMounted_=false;}
 void unlink(){unmount();if(host_){sdmmc_host_deinit();host_=false;}linked_=false;actualHz_=0;}
 const char*attempt(unsigned hz){unlink();card_={};sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.flags=SDMMC_HOST_FLAG_1BIT;host.max_freq_khz=hz/1000;host.command_timeout_ms=1000;
  esp_err_t e=sdmmc_host_init();if(e!=ESP_OK)return esp_err_to_name(e);host_=true;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;slot.clk=gpio_num_t(clk_);slot.cmd=gpio_num_t(cmd_);slot.d0=gpio_num_t(d0_);slot.flags|=SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
  if((e=sdmmc_host_init_slot(host.slot,&slot))!=ESP_OK||(e=sdmmc_card_init(&host,&card_))!=ESP_OK){unlink();return esp_err_to_name(e);}
  int khz=0;e=sdmmc_host_get_real_freq(host.slot,&khz);if(e!=ESP_OK){unlink();return esp_err_to_name(e);}actualHz_=unsigned(khz)*1000;
  if(fixedHz_&&actualHz_!=fixedHz_){unlink();return "fixed_clock_unavailable";}
  for(unsigned i=0;i<4;++i)if((e=sdmmc_get_status(&card_))!=ESP_OK){unlink();return esp_err_to_name(e);}linked_=true;return nullptr;
 }
 const char*probe(){if(fixedHz_)return attempt(fixedHz_);const char*error="sd_clock_unavailable";for(unsigned hz:{40000000u,20000000u,400000u}){if(hz>maxHz_)continue;error=attempt(hz);if(!error)return nullptr;}return error;}
 const char*mount(){if(!linked_)if(auto e=probe())return e;
  if(ff_diskio_get_drive(&drive_)!=ESP_OK)return "fat_drive_capacity";
  snprintf(driveName_,sizeof driveName_,"%u:",drive_);esp_vfs_fat_conf_t config{};config.base_path="/wish-sd";config.fat_drive=driveName_;config.max_files=8;
  auto e=esp_vfs_fat_register_cfg(&config,&fat_);if(e!=ESP_OK){fat_=nullptr;drive_=0xff;return esp_err_to_name(e);}ff_diskio_register_sdmmc(drive_,&card_);
  auto r=f_mount(fat_,driveName_,1);if(r!=FR_OK){unmount();return r==FR_NO_FILESYSTEM?"unformatted_or_unsupported_volume":r==FR_NOT_READY?"media_not_ready":"volume_mount_failed";}
  workerMounted_=true;return nullptr;
 }
 static const char*work(void*p,const std::atomic<bool>&cancel){auto&s=*static_cast<SdMmcVolume*>(p);std::lock_guard<std::recursive_mutex>lock(s.bus_);if(cancel)return "cancelled";
  const char*error=nullptr;if(!strcmp(s.method_,"probe"))error=s.probe();else if(!strcmp(s.method_,"mount"))error=s.mount();else if(!strcmp(s.method_,"unmount"))s.unlink();
  else if(!strcmp(s.method_,"space")){DWORD free;FATFS*fs=nullptr;if(f_getfree(s.driveName_,&free,&fs)!=FR_OK)error="space_scan_failed";else s.provider_.captureSpace(uint64_t(fs->n_fatent-2)*fs->csize*512,uint64_t(free)*fs->csize*512);}
  if(cancel&&(!strcmp(s.method_,"mount")||!strcmp(s.method_,"probe"))){s.unlink();error="cancelled";}s.lastError_=error;return error;
 }
 NativeOperation operation_;
public:
 SdMmcVolume(Core&c,Vfs&v,int clk,int cmd,int d0,unsigned maxHz):core_(c),vfs_(v),clk_(clk),cmd_(cmd),d0_(d0),maxHz_(maxHz),provider_(mounted_,bus_),operation_(c,"/dev/sd",{[](void(*run)(void*),void*ctx){auto e=new(std::nothrow)Entry{run,ctx};if(!e)return false;if(xTaskCreate(task,"wish-sdmmc",8192,e,1,nullptr)!=pdPASS){delete e;return false;}return true;},16384},this,work,[](void*p){auto&s=*static_cast<SdMmcVolume*>(p);s.mounted_=s.workerMounted_;if(s.mounted_&& !strcmp(s.method_,"mount"))s.provider_.volume(s.fat_->fs_type==FS_FAT32?"fat32":s.fat_->fs_type==FS_FAT16?"fat16":"fat12",s.fat_->csize*512);s.provider_.background(false);}){
  provider_.spaceRequester(this,[](void*p)->const char*{JsonDocument r;auto e=static_cast<SdMmcVolume*>(p)->invoke("space",JsonObjectConst(),r.to<JsonObject>());return e?e:"resource_busy";});
 }
 FileProvider&provider(){return provider_;}void poll(){operation_.poll();}
 void describe(JsonObject o)const{for(auto method:{"probe","mount","unmount","space"})NativeOperation::describe(o,method,"/dev/sd");o["transport"]="sdmmc";o["width_bits"]=1;o["probe"]["uses_file_contents"]=false;o["probe"]["writes_media"]=false;o["settings"]["clock_hz"]["auto"]=true;auto a=o["settings"]["clock_hz"]["fixed_choices"].to<JsonArray>();for(unsigned hz:{400000u,20000000u,40000000u})if(hz<=maxHz_)a.add(hz);o["settings"]["read_only"]="boolean";o["configuration_scope"]="device";o["application_boundary"]="unmounted_without_leases";o["clock_validation"]="protocol_status_only_not_sector_integrity_or_throughput";}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o){
  if(!strcmp(m,"describe")){describe(o);return nullptr;}
  if(!strcmp(m,"read")){o["transport"]="sdmmc";o["width_bits"]=1;o["mounted"]=mounted_;o["operation_pending"]=operation_.active();o["clock_mode"]=fixedHz_?"fixed":"auto";o["requested_hz"]=fixedHz_;o["read_only"]=readOnly_;if(!operation_.active()){o["link_ready"]=linked_;o["actual_hz"]=actualHz_;o["last_error"]=lastError_?lastError_:"";if(linked_){o["card"]["name"]=card_.cid.name;sizeText(o["card"].to<JsonObject>(),"capacity_bytes",uint64_t(card_.csd.capacity)*card_.csd.sector_size);}}operation_.read(o["operation"].to<JsonObject>());return nullptr;}
  if(!strcmp(m,"cancel"))return operation_.cancel(a);
  if(operation_.active())return "operation_active";
  if(!strcmp(m,"configure")){
   for(auto f:a)if(strcmp(f.key().c_str(),"clock_hz")&&strcmp(f.key().c_str(),"read_only"))return "unknown_storage_setting";
   unsigned hz=fixedHz_;bool ro=readOnly_;if(a.containsKey("clock_hz")){if(a["clock_hz"]=="auto")hz=0;else if(a["clock_hz"].is<unsigned>())hz=a["clock_hz"];else return "sd_clock_type";if(hz&&hz!=400000&&hz!=20000000&&hz!=40000000)return "fixed_clock_unavailable";if(hz>maxHz_)return "fixed_clock_unavailable";}
   if(a.containsKey("read_only")){if(!a["read_only"].is<bool>())return "storage_setting_type";ro=a["read_only"];}
   if(mounted_||linked_||vfs_.hasLeasesUnder(provider_.mount)||vfs_.pinned(provider_))return "storage_in_use";fixedHz_=hz;readOnly_=ro;provider_.writable=!ro;return nullptr;
  }
  if(strcmp(m,"probe")&&strcmp(m,"mount")&&strcmp(m,"unmount")&&strcmp(m,"space"))return "unsupported_method";
  if(a.size())return "unknown_storage_argument";if(strcmp(m,"space")&&(vfs_.hasLeasesUnder(provider_.mount)||vfs_.pinned(provider_)))return "storage_in_use";
  if(mounted_&&(!strcmp(m,"probe")||!strcmp(m,"mount")))return "volume_already_mounted";if(!mounted_&&!strcmp(m,"space"))return "volume_not_mounted";
  copy(method_,sizeof method_,m);provider_.background(true);auto e=operation_.start(m,o);if(e)provider_.background(false);return e;
 }
};
}
