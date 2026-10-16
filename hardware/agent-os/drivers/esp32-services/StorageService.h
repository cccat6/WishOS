// Included inside the BSP namespace. All file users share the same provider and SPI lock.
static const char*validateSdVolume(){
  if(sdWorkActive())return "resource_busy";
  if(!rail||!sdLink.ready)return "sd_link_unavailable";
  if(!mounted)return "volume_not_mounted";
  if(sdLink.outOfSpec&&!policies.get<bool>("/sys/bus","sd_allow_experimental_io",false))return "experimental_link_only";
  return nullptr;
}
static SdProvider sdFiles(SD,mounted,takeSPI,giveSPI,validateSdVolume);
static void sdSetWritable(bool enabled){sdCanWrite=enabled;if(!sdWorkActive())sdFiles.writable=enabled;}
// Called with the SPI lease held. Discovery only returns this snapshot.
static void sdVolumeSnapshot(bool ready){
  if(ready)sdFiles.captureMount(uint64_t(sdReference.csd.capacity())*512,
                                uint64_t(SD.clusterCount())*SD.bytesPerCluster());
  else sdFiles.invalidate();
}
static bool resolveFile(const char*input,char*full,const char*&relative,FileProvider*&provider){
  if(!normalizePath("/",input,full,256))return false;
  provider=vfs.resolve(full,relative);return provider!=nullptr;
}
static FileService fileService(vfs);
static const char*fsHandler(const char*m,JsonObjectConst a,JsonObject o){return fileService.invoke(m,a,o);}
static JsonDocument sdStatusSnapshot;static SdConfiguration sdRequestedConfiguration;
static char sdWorkMethod[12]{};
static const char*sdHandler(const char*,JsonObjectConst,JsonObject);
static const char*runSdWork(void*,const std::atomic<bool>&cancel){
 if(cancel)return "cancelled";
 if(!strcmp(sdWorkMethod,"space"))return sdFiles.scanSpace();
 if(!takeSPI())return "spi_busy";
 const char*error=ensureRailNative();
 if(!strcmp(sdWorkMethod,"rail")){giveSPI();return error;}
 if(!error&&(!strcmp(sdWorkMethod,"probe")||!sdTuned||!sdRequestedConfiguration.sameLink(sdLink.applied)))sdMountConfigured(sdRequestedConfiguration);
 if(!error&&cancel)error="cancelled";
 if(!error&&sdLink.ready&&!strcmp(sdWorkMethod,"mount"))sdMountVolume(sdRequestedConfiguration,true);
 if(!error)error=!sdLink.ready?sdLink.error:!strcmp(sdWorkMethod,"mount")&&!mounted?sdLink.volumeError:nullptr;
 giveSPI();return error;
}
static void sdWorkSettled(void*){sdFiles.writable=sdCanWrite;sdFiles.publish();sdStatusSnapshot.clear();}
struct SdWorkEntry{void(*run)(void*);void*context;};static SdWorkEntry sdWorkEntry;
static NativeOperation sdOperation(core,"/dev/sd",{
 [](void(*fn)(void*),void*context){sdWorkEntry={fn,context};return xTaskCreate([](void*){auto task=sdWorkEntry;task.run(task.context);vTaskDelete(nullptr);},"notes-sd",6144,nullptr,1,nullptr)==pdPASS;},8192},nullptr,runSdWork,sdWorkSettled);
static bool sdWorkActive(){return sdOperation.active();}
static bool sdWorkBlocksPeripherals(){return sdWorkActive()&&strcmp(sdWorkMethod,"space");}
static bool sdCancellationRequested(){return sdOperation.cancellation().load();}
static const char*requestSdSpace(){JsonDocument out;if(sdWorkActive())return "resource_busy";auto e=startSdWork("space",out.to<JsonObject>());return e?e:"resource_busy";}
static void pollSdWork(){sdOperation.poll();}
static const char*startSdWork(const char*method,JsonObject out){
 if(sdWorkActive())return "operation_active";
 if(strcmp(method,"rail")&&(shutdownRequested||pendingPower[0]))return "power_transition";
 if(strcmp(method,"rail")&&(audioSpooling||displayActive()||streaming||(strcmp(method,"space")&&captureRunning)))return "resource_busy";
 if(strcmp(method,"space")&&strcmp(method,"rail")&&(vfs.hasLeasesUnder("/storage/user")||vfs.pinnedPath("/storage/user")))return "storage_in_use";
 if(!strcmp(method,"space")){if(auto e=validateSdVolume())return e;}
 JsonDocument empty;sdStatusSnapshot.clear();sdHandler("read",empty.as<JsonObjectConst>(),sdStatusSnapshot.to<JsonObject>());
 sdRequestedConfiguration=sdConfiguration(policies);copy(sdWorkMethod,sizeof sdWorkMethod,method);
 sdFiles.background(sdWorkActive,requestSdSpace);
 return sdOperation.start(method,out);
}
static const char* sdHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"read")){
    if(sdWorkActive()){out.set(sdStatusSnapshot.as<JsonObjectConst>());out["state_snapshot"]="before_operation";sdOperation.read(out["operation"].to<JsonObject>());return nullptr;}
    sdOperation.read(out["operation"].to<JsonObject>());sdFiles.describe(out);out["powered"]=rail.load();out["mounted"]=mounted;
    out["presence"]=sdLink.ready?"card_link_ready":"unknown";
    out["tuned"]=sdTuned;
    out["link_ready"]=sdLink.ready;
    out["volume_attempted"]=sdVolumeAttempted;
    out["volume_error"]=sdLink.volumeError;
    if(mounted)out["partition"]=sdLink.partition;else out["partition"]=nullptr;
    if(sdLink.ready)sizeText(out,"physical_capacity_bytes",uint64_t(sdReference.csd.capacity())*512);
    JsonDocument configured;policies.resolve("/sys/bus","system",configured.to<JsonObject>());
    out["mode"]=configured["sd_spi_mode"];
    out["fixed_requested_hz"]=configured["sd_spi_hz"];
    out["auto_ceiling_hz"]=configured["sd_auto_ceiling_hz"];
    out["allow_experimental"]=configured["sd_allow_experimental"];
    out["allow_experimental_io"]=configured["sd_allow_experimental_io"];
    out["partition_choice"]=configured["sd_partition"];
    out["active_mode"]=sdLink.activeMode;
    out["active_requested_hz"]=sdLink.ready?sdLink.requestedHz:0;
    out["active_effective_hz"]=sdLink.ready?sdLink.effectiveHz:0;
    out["card_default_max_hz"]=sdLink.cardDefaultMaxHz;
    out["card_mode_max_hz"]=sdLink.cardModeMaxHz;
    out["high_speed_supported"]=sdLink.highSpeedSupported;
    out["high_speed_enabled"]=sdLink.highSpeedEnabled;
    out["out_of_spec"]=sdLink.outOfSpec;
    out["probe_method"]="register_handshake_no_user_blocks";
    out["handshake_rounds"]=12;
    out["clock_basis"]="arduino_spi_divider";
    out["read_only"]=sdLink.ready&&sdLink.readOnly;
    out["cached_validation"]=sdLink.ready&&sdLink.cacheHit;
    out["tested_candidates"]=sdLink.candidateCount;
    out["last_link_error"]=sdLink.error;
    auto desired=sdConfiguration(policies);
    out["link_configuration_pending"]=sdTuned&&!desired.sameLink(sdLink.applied);
    out["volume_configuration_pending"]=sdVolumeAttempted&&!desired.sameVolume(sdLink.applied);
    out["configuration_pending"]=(sdTuned&&!desired.sameLink(sdLink.applied))||
                                  (sdVolumeAttempted&&!desired.sameVolume(sdLink.applied));
    return nullptr;
  }
  if(!strcmp(m,"off"))return railOff();
  if(!strcmp(m,"cancel"))return sdOperation.cancel(a);
  if(!strcmp(m,"probe")||!strcmp(m,"mount")||!strcmp(m,"space")){
    if(a.size())return "unknown_sd_argument";
    return startSdWork(m,out);
  }
  return "use_fs_resource";
}
static const char* busHandler(const char*m,JsonObjectConst,JsonObject out){
  if(!strcmp(m,"speeds")){
    uint32_t source=getApbFrequency(),previous=0;
    out["source_hz"]=source;out["host_clock_limit_hz"]=source;
    auto speeds=out["sd_effective_hz"].to<JsonArray>();
    for(uint32_t divisor=1;divisor<=source/1000000u;++divisor){
      uint32_t requested=source/divisor;
      uint32_t effective=sdEffectiveHz(requested);
      if(effective<1000000u||effective>source||effective==previous)continue;
      speeds.add(effective);previous=effective;
    }
    out["count"]=speeds.size();
    return nullptr;
  }
  if(strcmp(m,"read"))return "unsupported_method";
  if(sdWorkActive()){out["operation_pending"]=true;out["display_spi_effective_hz"]=sdEffectiveHz(panelSpiHz);out["sd_link_ready"]=sdStatusSnapshot["link_ready"];out["sd_mounted"]=sdStatusSnapshot["mounted"];out["sd_active_effective_hz"]=sdStatusSnapshot["active_effective_hz"];return nullptr;}
  JsonDocument configured;policies.resolve("/sys/bus","system",configured.to<JsonObject>());
  out["display_spi_requested_hz"]=configured["display_spi_hz"];
  out["display_spi_effective_hz"]=sdEffectiveHz(panelSpiHz);
  out["clock_basis"]="arduino_spi_divider";
  out["display_configuration_pending"]=panelSpiHz!=(configured["display_spi_hz"]|16000000u);
  out["sd_spi_mode"]=configured["sd_spi_mode"];
  out["sd_spi_requested_hz"]=configured["sd_spi_hz"];
  out["sd_auto_ceiling_hz"]=configured["sd_auto_ceiling_hz"];
  out["sd_allow_experimental"]=configured["sd_allow_experimental"];
  out["sd_allow_experimental_io"]=configured["sd_allow_experimental_io"];
  out["sd_host_clock_limit_hz"]=getApbFrequency();
  out["sd_card_mode_max_hz"]=sdLink.cardModeMaxHz;
  out["sd_active_effective_hz"]=sdLink.ready?sdLink.effectiveHz:0;
  out["sd_link_ready"]=sdLink.ready;
  out["sd_mounted"]=mounted;
  return nullptr;
}
static const char*validatePlatformPolicy(const Policies&candidate){
  JsonDocument settings;candidate.resolve("/services/nfs","system",settings.to<JsonObject>());
  NfsOptions options;if(auto e=NfsOptions::parse(settings.as<JsonObjectConst>(),options))return e;
  if(!vfs.get(options.provider))return "unknown_nfs_provider";
  JsonDocument bus;candidate.resolve("/sys/bus","system",bus.to<JsonObject>());
  uint32_t displayHz=bus["display_spi_hz"]|16000000u,sdHz=bus["sd_spi_hz"]|20000000u;
  if(!sdClockExact(displayHz,sdEffectiveHz)||
     (!strcmp(bus["sd_spi_mode"]|"auto","fixed")&&!sdClockExact(sdHz,sdEffectiveHz)))
    return "unsupported_exact_spi_clock";
  JsonDocument storage;candidate.resolve("/sys/storage","system",storage.to<JsonObject>());
  if(!vfs.get(storage["cache_provider"]|"ram"))return "unknown_cache_provider";
  JsonDocument usage;ramFiles.describe(usage.to<JsonObject>());uint64_t used=0;
  parseOffset(usage["allocated_bytes"],used);
  if((storage["ram_quota_bytes"]|32768u)<used)return "quota_below_allocated";
  usage.clear();psramFiles.describe(usage.to<JsonObject>());parseOffset(usage["allocated_bytes"],used);
  if((storage["psram_quota_bytes"]|262144u)<used)return "quota_below_allocated";
  return nullptr;
}
static bool persistPolicies(const char*value,size_t n){return preferences.putString("policy",value)==n;}
static const char* policyHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"describe")||!strcmp(m,"read")){
    for(auto f:a)if(!Policies::choice("scope,resource,cursor,limit",f.key().c_str()))return "unknown_policy_argument";
    for(auto key:{"scope","resource"})if(a.containsKey(key)&&(!a[key].is<const char*>()||a[key].as<JsonString>().size()!=strlen(a[key].as<const char*>())))return "policy_string_required";
    for(auto key:{"cursor","limit"})if(a.containsKey(key)&&!a[key].is<unsigned>())return "policy_cursor_type";
    const char*scope=a["scope"]|"system",*resource=a["resource"]|static_cast<const char*>(nullptr);unsigned cursor=a["cursor"]|0u,limit=a["limit"]|(!strcmp(m,"describe")?8u:2u);if(!limit||limit>16||!Policies::validScope(scope))return "policy_read_range";
    if(!strcmp(m,"describe")){policies.describe(out["settings"].to<JsonArray>(),resource,cursor,limit);out["next_cursor"]=cursor+out["settings"].size();out["more"]=cursor+out["settings"].size()<policies.settingCount(resource);out["max_scopes"]=8;out["max_serialized_bytes"]=4096;}
    else{policies.read(scope,out,resource,cursor,limit);out["restore_valid"]=policyRestoreOk;}return nullptr;
  }
  if(!strcmp(m,"validate")||!strcmp(m,"apply")){
    if(!a["expected_boot_id"].isNull()&&strcmp(a["expected_boot_id"]|"",bootId))return "boot_changed";
    bool busChanged=false,sdChanged=false;
    JsonDocument currentBus;policies.resolve("/sys/bus","system",currentBus.to<JsonObject>());
    for(auto c:a["changes"].as<JsonArrayConst>())if(!strcmp(c["resource"]|"","/sys/bus")){
      busChanged=true;
      for(auto field:c["settings"].as<JsonObjectConst>())
        if(!strncmp(field.key().c_str(),"sd_",3)&&field.value()!=currentBus[field.key()])sdChanged=true;
    }
    if(busChanged&&sdWorkActive()&&!strcmp(m,"apply"))return "storage_preparing";
    bool sdLocked=false;
    if(sdChanged&&!strcmp(m,"apply")){
      if(audioRunning||audioSpooling||displayActive()||streaming||vfs.hasLeasesUnder("/storage/user")||vfs.pinnedPath("/storage/user"))return "resource_busy";
      if(!takeSPI())return "spi_busy";
      sdLocked=true;
    }
    auto e=policies.apply(a,out,!strcmp(m,"validate"),persistPolicies);
    if(!e&&sdLocked){SD.vol()->end();mounted=false;sdVolumeAttempted=false;sdVolumeSnapshot(false);}
    if(sdLocked)giveSPI();
    if(!e&&busChanged){out["sd_apply_time"]="next_probe";out["display_apply_time"]="idle_boundary";}
    if(!e&&!strcmp(m,"apply")){

      applyPanelClockIfIdle();
      if(busChanged&&sdLink.ready&&!strcmp(sdLink.volumeError,"experimental_link_only")&&
         policies.get<bool>("/sys/bus","sd_allow_experimental_io",false))
        sdVolumeAttempted=false;
      ramFiles.budget(policies.get<uint32_t>("/sys/storage","ram_quota_bytes",32768));
      psramFiles.budget(policies.get<uint32_t>("/sys/storage","psram_quota_bytes",262144));
      event("/sys/config","changed");
    }return e;
  }return "unsupported_method";
}
static const char* storageHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"read")||!strcmp(m,"list"))return describeEspStorage(vfs,a,out);
  return "unsupported_method";
}
static bool shutdownOnLowBattery(){JsonDocument d;policies.resolve("/sys/power","system",d.to<JsonObject>());return !strcmp(d["low_battery_action"]|"notify","shutdown");}
static double cpuPercent=0;static bool cpuKnown=false;static uint64_t statsAt=0;static uint32_t statsWindow=0;
static void sampleStats(){
  uint64_t now=uptime();if(now-statsAt<policies.get<uint32_t>("/sys/stats","sample_ms",1000))return;
#if configGENERATE_RUN_TIME_STATS && configUSE_TRACE_FACILITY
  TaskStatus_t states[32];configRUN_TIME_COUNTER_TYPE total=0;UBaseType_t n=uxTaskGetSystemState(states,32,&total);
  static configRUN_TIME_COUNTER_TYPE previous=0,previousIdle=0;configRUN_TIME_COUNTER_TYPE idle=0;
  for(UBaseType_t i=0;i<n;++i)if(!strncmp(states[i].pcTaskName,"IDLE",4))idle+=states[i].ulRunTimeCounter;
  auto elapsed=total-previous,rest=idle-previousIdle;
  cpuKnown=n&&previous&&elapsed&&rest<=elapsed;if(cpuKnown)cpuPercent=100.0*(double(elapsed-rest)/double(elapsed));previous=total;previousIdle=idle;
#endif
  statsWindow=uint32_t(now-statsAt);statsAt=now;
  JsonDocument reading;auto sample=reading.to<JsonObject>();sample["heap_free_bytes"]=ESP.getFreeHeap();sample["largest_free_block_bytes"]=ESP.getMaxAllocHeap();if(cpuKnown)sample["cpu_percent"]=cpuPercent;else sample["cpu_percent"]=nullptr;sample["audio_queue_blocks"]=audioDelivery.queued();emit("/sys/stats","sample",sample);
}
static const char* statsHandler(const char*,JsonObjectConst,JsonObject o){
  o["at_ms"]=statsAt;o["window_ms"]=statsWindow;o["heap_free_bytes"]=ESP.getFreeHeap();o["heap_min_free_bytes"]=ESP.getMinFreeHeap();o["largest_free_block_bytes"]=ESP.getMaxAllocHeap();o["clock_mhz"]=getCpuFrequencyMhz();if(cpuKnown)o["cpu_percent"]=cpuPercent;else{o["cpu_percent"]=nullptr;o["cpu_reason"]="sampling_unavailable_or_warmup";}
  o["psram_bytes"]=ESP.getPsramSize();o["psram_free_bytes"]=ESP.getFreePsram();o["flash_bytes"]=ESP.getFlashChipSize();o["firmware_bytes"]=ESP.getSketchSize();o["event_drops"]=eventDrops.load()+core.eventOverruns();o["audio_dropped_samples"]=audioDrops.load();o["audio_queue_blocks"]=audioDelivery.queued();o["display_active"]=bool(displayActive());o["card_mounted"]=sdFiles.available();return nullptr;
}

static int inputGate(const Event&e){
  // Input policy follows the presentation lifecycle, not one waveform phase.
  // A terminal fault must not prevent the user from invoking recovery controls.
  if(!displayInputBusy())return 0;JsonDocument d;policies.resolve("/sys/input",appScope,d.to<JsonObject>());
  if(!Policies::choice(d["gated_sources"]|"",e.path))return 0;
  const char*mode=d["during_display_busy"]|"dispatch";
  return !strcmp(mode,"queue")?1:!strcmp(mode,"drop")?2:0;
}

static const char*cacheHandler(const char*m,JsonObjectConst a,JsonObject out){
 JsonDocument d;policies.resolve("/sys/storage",a["scope"]|"system",d.to<JsonObject>());
 const char*id=a["provider"]|(d["cache_provider"]|"ram");auto p=vfs.get(id);if(!p)return "unknown_cache_provider";
 if(strcmp(m,"resolve")&&strcmp(m,"prepare"))return "unsupported_method";
 if(auto e=resolveCachePath(*p,a["key"]|"",out))return e;
 if(!strcmp(m,"prepare")){if(auto e=prepareCacheDirectory(*p))return e;out["directory_prepared"]=true;}
 return nullptr;
}

static const char*nfsHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"stop")){bool was=nfsService.active();if(auto error=nfsService.stop())return error;if(was)event("/services/nfs","stopped");}
  else if(!strcmp(m,"memory"))return nfsService.memory(a,out);
  else if(!strcmp(m,"start")){
    if(nfsService.active())return "nfs_already_running";
    if(pendingPower[0]||shutdownRequested)return "power_transition";
    if(WiFi.getMode()==WIFI_OFF)return "network_unavailable";
    JsonDocument options;policies.resolve("/services/nfs","system",options.to<JsonObject>());
    for(auto field:a){auto s=policies.find("/services/nfs",field.key().c_str());if(!s)return "unknown_nfs_setting";
      if(auto e=policies.check(*s,field.value()))return e;options[field.key()]=field.value();}
    NfsOptions parsed;if(auto e=NfsOptions::parse(options.as<JsonObjectConst>(),parsed))return e;
    auto provider=vfs.get(parsed.provider);if(!provider)return "unknown_provider";
    if(provider==&sdFiles){if(auto e=ensureSdReady())return e;}
    uint64_t epoch=(uint64_t(esp_random())<<32)|esp_random();
    if(auto e=nfsService.start(parsed,nfsTransport,epoch?epoch:1))return e;
    event("/services/nfs","started",parsed.provider);
  }else if(strcmp(m,"read"))return "unsupported_method";
  nfsService.read(out);JsonDocument desired;policies.resolve("/services/nfs","system",desired.to<JsonObject>());
  out["configured"]=desired.as<JsonObjectConst>();out["settings_apply_time"]="next_service_start";
  out["configuration_pending"]=nfsService.active()&&out["active"]!=desired.as<JsonVariantConst>();
  out["ip"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString();
  return nullptr;
}
