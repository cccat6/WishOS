// Included inside the BSP namespace. All file users share the same provider and SPI lock.
static SdProvider sdFiles(SD,mounted,takeSPI,giveSPI,ensureSdReady);
static void sdSetWritable(bool enabled){sdFiles.writable=enabled;}
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
static const char* fsHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"mounts")){vfs.describe(out["providers"].to<JsonArray>());return nullptr;}
  char full[256];const char*rel;FileProvider*p;
  if(!resolveFile(a["file"]|(a["path"]|"/storage/user"),full,rel,p))return "unknown_mount_or_path";
  if(p==&sdFiles&&!mounted){if(!takeSPI())return "spi_busy";auto e=ensureSdReady();giveSPI();if(e)return e;}
  if(!p->available())return "provider_unavailable";
  if(!strcmp(m,"stat")){FileInfo info;if(auto e=p->stat(rel,info))return e;out["file"]=full;sizeText(out,"size",info.size);out["directory"]=info.directory;return nullptr;}
  if(!strcmp(m,"list")){uint32_t limit=a["limit"]|12u;if(!limit||limit>24)return "list_limit";return p->list(rel,a["cursor"]|0u,limit,out);}
  uint64_t offset;if(!parseOffset(a["offset"],offset))return "invalid_offset";
  if(!strcmp(m,"read")){uint32_t n=a["length"]|1024u;if(!n||n>1024)return "chunk_limit";uint8_t bytes[1024];char h[2049];size_t got;
    if(auto e=p->read(rel,offset,bytes,n,got))return e;hex(bytes,got,h);out["hex"]=h;sizeText(out,"next_offset",offset+got);return nullptr;}
  if(!p->writable)return "read_only";
  if(p->caseInsensitive()&&strchr(rel,'~'))return "unsupported_short_alias";
  if(!vfs.canMutate(full,p->caseInsensitive()))return "file_in_use";
  if(!strcmp(m,"write")){uint8_t bytes[1024];size_t n,wrote;if(a["text"].is<const char*>()){const char*text=a["text"];n=strlen(text);if(n>sizeof(bytes))return "chunk_limit";memcpy(bytes,text,n);}else if(!unhex(a["hex"]|"",bytes,sizeof(bytes),n))return "invalid_hex";
    if(auto e=p->write(rel,offset,bytes,n,a["truncate"]|false,wrote))return e;out["written"]=wrote;sizeText(out,"next_offset",offset+wrote);return nullptr;}
  if(!strcmp(m,"remove"))return p->remove(rel);
  if(!strcmp(m,"mkdir"))return p->mkdir(rel);
  if(!strcmp(m,"rename")){char dest[256];const char*dr;FileProvider*dp;if(!resolveFile(a["to"]|"",dest,dr,dp))return "invalid_destination";if(dp!=p)return "cross_volume_rename";if(dp->caseInsensitive()&&strchr(dr,'~'))return "unsupported_short_alias";if(!vfs.canMutate(dest,dp->caseInsensitive()))return "file_in_use";return p->rename(rel,dr);}
  return "unsupported_method";
}
static const char* sdHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"read")){
    sdFiles.describe(out);out["powered"]=rail;out["mounted"]=mounted;
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
  if(!strcmp(m,"probe")){
    if(vfs.hasLeasesUnder("/storage/user")||vfs.pinnedPath("/storage/user"))return "storage_in_use";
    if(audioRunning||audioSpooling||displayActive()||streaming)return "resource_busy";
    if(!takeSPI())return "spi_busy";
    auto e=ensureRail();
    if(!e)sdMountConfigured();
    giveSPI();
    if(e)return e;
    sdHandler("read",a,out);
    return sdLink.ready?nullptr:"sd_link_failed";
  }
  if(!strcmp(m,"mount")){
    if(vfs.hasLeasesUnder("/storage/user")||vfs.pinnedPath("/storage/user"))return "storage_in_use";
    if(audioRunning||audioSpooling||displayActive()||streaming)return "resource_busy";
    if(!takeSPI())return "spi_busy";
    auto e=ensureRail();
    if(!e&&(!sdTuned||!sdConfiguration(policies).sameLink(sdLink.applied)))sdMountConfigured();
    if(!e&&sdLink.ready)sdMountVolume(true);
    giveSPI();
    sdHandler("read",a,out);
    return e?e:mounted?nullptr:sdLink.ready?"volume_unavailable":"sd_link_failed";
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
  if(!strcmp(m,"describe")){policies.describe(out["settings"].to<JsonArray>());out["max_scopes"]=8;out["max_serialized_bytes"]=4096;return nullptr;}
  if(!strcmp(m,"read")){policies.read(a["scope"]|"system",out);out["restore_valid"]=policyRestoreOk;return nullptr;}
  if(!strcmp(m,"validate")||!strcmp(m,"apply")){
    if(!a["expected_boot_id"].isNull()&&strcmp(a["expected_boot_id"]|"",bootId))return "boot_changed";
    bool busChanged=false,sdChanged=false;
    JsonDocument currentBus;policies.resolve("/sys/bus","system",currentBus.to<JsonObject>());
    for(auto c:a["changes"].as<JsonArrayConst>())if(!strcmp(c["resource"]|"","/sys/bus")){
      busChanged=true;
      for(auto field:c["settings"].as<JsonObjectConst>())
        if(!strncmp(field.key().c_str(),"sd_",3)&&field.value()!=currentBus[field.key()])sdChanged=true;
    }
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
  if(!strcmp(m,"read")||!strcmp(m,"list")){vfs.describe(out["providers"].to<JsonArray>());
    out["internal_heap_free"]=ESP.getFreeHeap();out["psram_physical_bytes"]=ESP.getPsramSize();out["psram_free"]=ESP.getFreePsram();
    out["flash_physical_bytes"]=ESP.getFlashChipSize();out["firmware_bytes"]=ESP.getSketchSize();out["partition_resize_supported"]=false;
    auto partitions=out["flash_partitions"].to<JsonArray>();auto it=esp_partition_find(ESP_PARTITION_TYPE_ANY,ESP_PARTITION_SUBTYPE_ANY,nullptr);
    while(it){auto part=esp_partition_get(it);auto item=partitions.add<JsonObject>();item["label"]=part->label;item["type"]=int(part->type);item["subtype"]=int(part->subtype);sizeText(item,"offset",part->address);sizeText(item,"capacity_bytes",part->size);item["encrypted"]=part->encrypted;it=esp_partition_next(it);}return nullptr;}
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
  JsonDocument reading;auto sample=reading.to<JsonObject>();sample["heap_free_bytes"]=ESP.getFreeHeap();sample["largest_free_block_bytes"]=ESP.getMaxAllocHeap();if(cpuKnown)sample["cpu_percent"]=cpuPercent;else sample["cpu_percent"]=nullptr;sample["audio_queue_blocks"]=uxQueueMessagesWaiting(audioQueue);emit("/sys/stats","sample",sample);
}
static const char* statsHandler(const char*,JsonObjectConst,JsonObject o){
  o["at_ms"]=statsAt;o["window_ms"]=statsWindow;o["heap_free_bytes"]=ESP.getFreeHeap();o["heap_min_free_bytes"]=ESP.getMinFreeHeap();o["largest_free_block_bytes"]=ESP.getMaxAllocHeap();o["clock_mhz"]=getCpuFrequencyMhz();if(cpuKnown)o["cpu_percent"]=cpuPercent;else{o["cpu_percent"]=nullptr;o["cpu_reason"]="sampling_unavailable_or_warmup";}
  o["psram_bytes"]=ESP.getPsramSize();o["psram_free_bytes"]=ESP.getFreePsram();o["flash_bytes"]=ESP.getFlashChipSize();o["firmware_bytes"]=ESP.getSketchSize();o["event_drops"]=eventDrops.load();o["audio_dropped_samples"]=audioDrops.load();o["audio_queue_blocks"]=uxQueueMessagesWaiting(audioQueue);o["display_active"]=bool(displayActive());o["card_mounted"]=mounted;return nullptr;
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
    if(provider==&sdFiles){
      if(audioRunning||audioSpooling||displayActive()||streaming)return "resource_busy";
      if(!takeSPI())return "spi_busy";auto e=ensureSdReady();giveSPI();if(e)return e;
      // This is an explicitly requested volume service, not a presence test.
      if(!sdFiles.freeKnown){if(auto e=sdFiles.refreshSpace())return e;}
    }
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
