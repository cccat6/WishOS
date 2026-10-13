static bool panelReady(){return digitalRead(pins::BUSY)==HIGH;}
static void panelReset(bool high){digitalWrite(pins::RESET,high?HIGH:LOW);}
static bool panelCommand(uint8_t command,const uint8_t*bytes,size_t n){
  if(!panelReady()||!takeSPI())return false;
  if(!panelReady()){giveSPI();return false;}
  digitalWrite(pins::SD_CS,HIGH);SPI.beginTransaction(SPISettings(panelSpiHz,MSBFIRST,SPI_MODE0));
  digitalWrite(pins::DC,LOW);digitalWrite(pins::PANEL_CS,LOW);SPI.transfer(command);digitalWrite(pins::PANEL_CS,HIGH);
  if(n){digitalWrite(pins::DC,HIGH);digitalWrite(pins::PANEL_CS,LOW);SPI.writeBytes(bytes,n);digitalWrite(pins::PANEL_CS,HIGH);}
  SPI.endTransaction();giveSPI();return true;
}
static bool panelData(const uint8_t*bytes,size_t n){
  if(!panelReady()||!takeSPI())return false;if(!panelReady()){giveSPI();return false;}
  digitalWrite(pins::SD_CS,HIGH);SPI.beginTransaction(SPISettings(panelSpiHz,MSBFIRST,SPI_MODE0));
  digitalWrite(pins::DC,HIGH);digitalWrite(pins::PANEL_CS,LOW);SPI.writeBytes(bytes,n);digitalWrite(pins::PANEL_CS,HIGH);
  SPI.endTransaction();giveSPI();return true;
}
struct Presentation {PresentationState::Stamp stamp;bool latestOnly=true,maintenance=false;UiView view;uint32_t id=0;nm::Scene*scene=nullptr;char file[256]{};bool accurate=false,shutdown=false,force=false,ui=false,deduplicate=true;uint32_t timeout=60000,streamTimeout=15000;};
static Presentation pendingDisplays[4],currentDisplay;
static bool presentationCurrent(const Presentation&);
static const char*defaultUiDriver(){return "e6.ink6.ui-v1";}
static const char*validateUiDriver(const char*id){return strcmp(id,"e6.ink6.ui-v1")&&strcmp(id,"e6.ink6.image-v1")?"unsupported_display_driver":nullptr;}
static unsigned pendingCount=0;static nm::Stripe renderStripe;
static char pendingHash[65]{};static bool commitRequested=false,hashLive=false,discardStream=false;
static uint64_t displayBegan=0;static uint32_t renderedBytes=0;
static void receipt(uint32_t id,bool ok,const char*state,const char*error="none",uint32_t ms=0){
  auto&j=jobs[id%8];if(j.id!=id)return;j.complete=true;j.ok=ok;j.total=ms;copy(j.state,sizeof(j.state),state);copy(j.error,sizeof(j.error),error);
  JsonDocument d;auto o=d.to<JsonObject>();if(currentDisplay.id==id){o["view_id"]=currentDisplay.stamp.view;o["view_revision"]=currentDisplay.stamp.revision;o["activation"]=currentDisplay.stamp.activation;}o["job_id"]=id;o["state"]=state;o["success"]=ok;o["error"]=error;o["total_ms"]=ms;emit("/dev/display",ok?"completed":"failed",o);
}
static void resetDigest(){if(hashLive)mbedtls_sha256_free(&digest);mbedtls_sha256_init(&digest);mbedtls_sha256_starts(&digest,0);hashLive=true;}
static void finishDisplay(bool ok,const char*state,const char*error){
  vfs.release(currentDisplay.id);
  receipt(currentDisplay.id,ok,state,error,displayActive?uint32_t(uptime()-displayBegan):0);
  if(ok){if(currentDisplay.stamp.view)presentationState.completed(currentDisplay.stamp);presentedJob=currentDisplay.id;copy(uiPresented,sizeof(uiPresented),currentDisplay.ui?currentDisplay.file:"");}
  bool cut=currentDisplay.shutdown;if(currentDisplay.scene)delete currentDisplay.scene;currentDisplay={};
  displayActive=false;streaming=false;commitRequested=false;discardStream=false;
  if(hashLive){mbedtls_sha256_free(&digest);hashLive=false;}
  if(ok){lastRefreshRTC=esp_clk_rtc_time();lastClear=millis();}else frameValid=false;
  copy(displayState,sizeof(displayState),state);copy(displayError,sizeof(displayError),error);
  if(cut){if(ok)shutdownReady=true;else shutdownRequested=false;}
}
static const char*beginCurrent(){
  applyPanelClockIfIdle();
  if(currentDisplay.file[0]&&!vfs.lockRead(currentDisplay.file,currentDisplay.id))return "asset_lease_capacity";
  uiLeaseOwner=currentDisplay.id;
  if(currentDisplay.ui&&!prepareUi(currentDisplay.file,currentDisplay.view))return uiError;
  if(!takeSPI())return "spi_busy";auto e=ensureRail();giveSPI();if(e)return e;
  if(!panel.start(currentDisplay.accurate,uptime(),currentDisplay.timeout))return "display_not_ready";
  displayBegan=uptime();renderedBytes=0;resetDigest();copy(displayState,sizeof(displayState),"preparing");displayActive=true;return nullptr;
}
static const char* queueDisplay(nm::Payload*payload,uint8_t profile,bool shutdown,JsonObject out,const char*scope=appScope,PresentationState::Stamp stamp={}){
  if(shutdownRequested){delete payload;return "shutdown_in_progress";}
  auto scene=new(std::nothrow) nm::Scene;
  if(!scene){delete payload;return "memory_low";}
  if(!nm::validate(*payload,*scene)){delete scene;delete payload;return "invalid_scene";}delete payload;
  JsonDocument cfg;policies.resolve("/dev/display",scope,cfg.to<JsonObject>());const char*strategy=cfg["pending"]|"latest";
  if((displayActive||streaming)&&!strcmp(strategy,"reject")){delete scene;return "display_busy";}
  if(!strcmp(strategy,"latest"))while(pendingCount){auto&old=pendingDisplays[--pendingCount];receipt(old.id,false,"superseded","newer_presentation");vfs.release(old.id);delete old.scene;old={};}
  if(pendingCount>=unsigned(cfg["queue_limit"]|2)){delete scene;return "display_queue_full";}
  Presentation j;j.stamp=stamp;j.latestOnly=!strcmp(strategy,"latest");j.id=nextJob++;j.scene=scene;j.accurate=profile!=6;j.shutdown=shutdown;j.force=shutdown;j.timeout=cfg["timeout_ms"]|60000;j.deduplicate=cfg["deduplicate"]|true;
  rememberJob(j.id);pendingDisplays[pendingCount++]=j;out["job_id"]=j.id;out["status"]="queued";return nullptr;
}
static void drivers(JsonArray arr){
  for(int i=0;i<2;++i){auto d=arr.add<JsonObject>();d["id"]=i?"e6.ink6.image-v1":"e6.ink6.ui-v1";d["color_space"]="e6.ink6/v1";d["color_count"]=6;d["width"]=480;d["height"]=720;
    d["input_format"]="native720x480-4bpp";d["frame_bytes"]=E6Driver::FRAME_BYTES;d["estimated_present_ms"]=i?30000:16000;d["timing_source"]="previous_board_observation";d["validated_on_this_board"]=false;d["cancel_while_presenting"]=false;
    d["recommended_for"]=i?"photos and color-sensitive content":"text, status and simple colored UI";auto colors=d["colors"].to<JsonArray>();for(int c:{0,1,2,3,5,6})colors.add(c);
    auto palette=d["palette"].to<JsonArray>();const int codes[]={0,1,2,3,5,6};const char*names[]={"black","white","yellow","red","blue","green"};const char*rgb[]={"#000000","#ffffff","#ffff00","#ff0000","#0000ff","#00ff00"};for(int n=0;n<6;++n){auto color=palette.add<JsonObject>();color["code"]=codes[n];color["name"]=names[n];color["nominal_rgb"]=rgb[n];}d["palette_calibrated"]=false;d["native_band_bytes"]=5760;
  }
}
static const char*validateDisplayArguments(const char*m,JsonObjectConst a){
 const char*allowed=Policies::choice("read,drivers,recover",m)?"":!strcmp(m,"chunk")?"stream_id,offset,hex":!strcmp(m,"commit")?"stream_id,sha256":!strcmp(m,"abort")?"stream_id":"view_id,view_revision,driver,scope,force,file,scene";
 for(auto f:a)if(!Policies::choice(allowed,f.key().c_str()))return "unknown_display_argument";
 if(a.containsKey("file")&&strcmp(m,"present"))return "unknown_display_argument";if(a.containsKey("scene")&&strcmp(m,"scene"))return "unknown_display_argument";
 if(a.containsKey("sha256")&&!a["sha256"].isNull()&&(!a["sha256"].is<const char*>()||strlen(a["sha256"].as<const char*>())!=64))return "hash_format";
 for(auto key:{"driver","scope","file","hex"})if(a.containsKey(key)&&!a[key].is<const char*>())return "display_argument_type";
 for(auto key:{"view_id","view_revision","stream_id","offset"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "display_argument_type";
 if(a.containsKey("force")&&!a["force"].is<bool>())return "display_argument_type";
 if(a.containsKey("scope")&&!Policies::validScope(a["scope"]))return "invalid_scope";
 return nullptr;
}
static const char* displayHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(auto e=validateDisplayArguments(m,a))return e;
  if(!strcmp(m,"drivers")){drivers(out["drivers"].to<JsonArray>());return nullptr;}
  if(!strcmp(m,"read")){out["state"]=panel.faulted()?"fault":displayState;out["ready"]=panelReady();out["receiving"]=panel.receiving();out["active"]=bool(displayActive);out["streaming"]=bool(streaming);out["offset"]=panel.bytes();out["stream_id"]=streamId;out["job_id"]=currentDisplay.id;out["pending"]=pendingCount;out["hash"]=frameValid?frameHash:"";out["error"]=panel.faulted()?panel.error():displayError;return nullptr;}
  if(!strcmp(m,"recover")){if(!panel.clearFault(uptime()))return "display_not_stably_ready";frameValid=false;copy(displayError,sizeof(displayError),"none");copy(displayState,sizeof(displayState),"idle");return nullptr;}
  JsonDocument cfg;policies.resolve("/dev/display",a["scope"]|appScope,cfg.to<JsonObject>());
  const char*driver=a["driver"]|(cfg["driver"]|"e6.ink6.ui-v1");bool accurate=!strcmp(driver,"e6.ink6.image-v1");if(!accurate&&strcmp(driver,"e6.ink6.ui-v1"))return "unsupported_display_driver";
  if(!a["profile"].isNull())return "use_display_driver_id";
  PresentationState::Stamp stamp;if(!strcmp(m,"present")||!strcmp(m,"scene")||!strcmp(m,"clear")||!strcmp(m,"begin")){if(auto e=displayViewStamp(a,stamp))return e;}
  if(!strcmp(m,"present")){
    char full[256];const char*rel;FileProvider*p;if(!resolveFile(a["file"]|"",full,rel,p))return "invalid_file_path";FileInfo info;if(auto e=p->stat(rel,info))return e;if(info.directory||info.size!=E6Driver::FRAME_BYTES)return "native_frame_size";
    auto e=queueUi(full,driver,a["force"]|false,out,a["scope"]|appScope,nullptr,stamp);if(!e&&pendingCount)pendingDisplays[pendingCount-1].ui=false;return e;
  }
  if(!strcmp(m,"scene")||!strcmp(m,"clear")){
    auto p=new(std::nothrow) nm::Payload;if(!p)return "memory_low";
    if(!strcmp(m,"clear")){const char*s="{\"format\":\"scene-v1\",\"background\":1,\"commands\":[]}";p->size=strlen(s);memcpy(p->bytes,s,p->size);}else{auto s=a["scene"].as<JsonObjectConst>();if(s.isNull()||measureJson(s)>board::MAX_SCENE){delete p;return "scene_limit";}p->size=serializeJson(s,p->bytes,sizeof(p->bytes));}
    auto e=queueDisplay(p,accurate?0xff:6,false,out,a["scope"]|appScope,stamp);if(!e&&pendingCount)pendingDisplays[pendingCount-1].force=a["force"]|false;return e;
  }
  if(!strcmp(m,"begin")){
    if(displayActive||streaming||pendingCount||panel.faulted()||shutdownRequested)return "display_busy";
    currentDisplay={};currentDisplay.stamp=stamp;currentDisplay.id=nextJob++;currentDisplay.accurate=accurate;currentDisplay.timeout=cfg["timeout_ms"]|60000;currentDisplay.force=a["force"]|false;currentDisplay.deduplicate=cfg["deduplicate"]|true;currentDisplay.streamTimeout=cfg["stream_timeout_ms"]|15000u;
    rememberJob(currentDisplay.id);if(auto e=beginCurrent()){receipt(currentDisplay.id,false,"failed",e);vfs.release(currentDisplay.id);currentDisplay={};return e;}
    streaming=true;streamId=nextStream++;streamDeadline=millis()+currentDisplay.streamTimeout;
    out["stream_id"]=streamId;out["job_id"]=currentDisplay.id;out["status"]="preparing";out["chunk_limit"]=1024;return nullptr;
  }
  if(!strcmp(m,"chunk")||!strcmp(m,"commit")||!strcmp(m,"abort")){
    if(!streaming||a["stream_id"].as<uint32_t>()!=streamId)return "stream_not_found";
    if(!presentationCurrent(currentDisplay))return "view_changed";
    if(!strcmp(m,"abort")){if(!panel.receiving())return "not_cancellable";panel.cancel();finishDisplay(false,"cancelled","cancelled");return nullptr;}
    if(!panel.receiving())return "display_not_receiving";
    if(!strcmp(m,"chunk")){uint8_t b[1024];size_t n;if(!a["offset"].is<uint32_t>()||a["offset"].as<uint32_t>()!=panel.bytes())return "wrong_offset";if(!unhex(a["hex"]|"",b,sizeof(b),n))return "invalid_hex";if(!panel.write(b,n))return "chunk_not_accepted";mbedtls_sha256_update(&digest,b,n);streamDeadline=millis()+currentDisplay.streamTimeout;out["next_offset"]=panel.bytes();return nullptr;}
    if(panel.bytes()!=E6Driver::FRAME_BYTES)return "incomplete_frame";finishHash(pendingHash);hashLive=false;
    if(!a["sha256"].isNull()&&(!a["sha256"].is<const char*>()||strcmp(a["sha256"].as<const char*>(),pendingHash))){panel.cancel();finishDisplay(false,"failed","hash_mismatch");return "hash_mismatch";}
    streaming=false;commitRequested=true;out["job_id"]=currentDisplay.id;out["status"]="queued";return nullptr;
  }
  return "unsupported_method";
}
static bool presentationCurrent(const Presentation&job){if(!job.stamp.view)return job.shutdown||job.maintenance;auto now=presentationState.stamp(job.stamp.view);return now.activation&&now.activation==job.stamp.activation&&(!job.latestOnly||now.revision==job.stamp.revision);}
static void pollDisplay(){
  pollUiPresentation();
  while(pendingCount&&!presentationCurrent(pendingDisplays[0])){auto&job=pendingDisplays[0];receipt(job.id,false,"superseded","view_changed");vfs.release(job.id);delete job.scene;for(unsigned i=1;i<pendingCount;++i)pendingDisplays[i-1]=pendingDisplays[i];pendingDisplays[--pendingCount]={};}
  if(!displayActive&&pendingCount&&!panel.faulted()){
    currentDisplay=pendingDisplays[0];const char*e=beginCurrent();
    if(e&&!strcmp(e,"spi_busy")){vfs.release(currentDisplay.id);uiLeaseOwner=0;currentDisplay={};return;}
    for(unsigned i=1;i<pendingCount;++i)pendingDisplays[i-1]=pendingDisplays[i];--pendingCount;
    if(e){finishDisplay(false,"failed",e);return;}
  }
  if(!displayActive)return;panel.tick(uptime());
  if(panel.faulted()){finishDisplay(false,"failed",panel.error());return;}
  if(panel.receiving()){
    if(!presentationCurrent(currentDisplay)){panel.cancel();finishDisplay(false,"superseded","view_changed");return;}
    copy(displayState,sizeof(displayState),"receiving");
    if((currentDisplay.scene||currentDisplay.ui||currentDisplay.file[0])&&!commitRequested){
      if(currentDisplay.ui){if(!renderUiBand(renderedBytes/5760*16,renderStripe)){if(!strcmp(uiError,"spi_busy"))return;panel.cancel();finishDisplay(false,"failed",uiError);return;}}
      else if(currentDisplay.scene){renderStripe.startColumns(renderedBytes/5760*16,currentDisplay.scene->background);renderStripe.render(*currentDisplay.scene);}
      else{const char*rel;auto p=vfs.resolve(currentDisplay.file,rel);size_t got=0;auto e=p?p->read(rel,renderedBytes,renderStripe.bytes,5760,got):"source_removed";if(e&&!strcmp(e,"spi_busy"))return;if(e||got!=5760){panel.cancel();finishDisplay(false,"failed",e?e:"source_short_read");return;}
        for(size_t i=0;i<got;++i){uint8_t hi=renderStripe.bytes[i]>>4,lo=renderStripe.bytes[i]&15;if(hi>6||lo>6||hi==4||lo==4){panel.cancel();finishDisplay(false,"failed","invalid_native_colors");return;}}}
      if(panel.write(renderStripe.bytes,5760)){mbedtls_sha256_update(&digest,renderStripe.bytes,5760);renderedBytes+=5760;}
      if(renderedBytes==E6Driver::FRAME_BYTES){finishHash(pendingHash);hashLive=false;commitRequested=true;}
    }
    if(streaming&&int32_t(millis()-streamDeadline)>=0){panel.cancel();finishDisplay(false,"failed","stream_timeout");return;}
    if(commitRequested){
      uint8_t identity=currentDisplay.accurate?0xff:6;
      if(!currentDisplay.force&&currentDisplay.deduplicate&&esp_clk_rtc_time()-lastRefreshRTC<86220000000ULL&&frameValid&&frameProfile==identity&&!strcmp(frameHash,pendingHash)){
        panel.cancel();vfs.release(currentDisplay.id);if(currentDisplay.stamp.view)presentationState.completed(currentDisplay.stamp);presentedJob=currentDisplay.id;copy(uiPresented,sizeof(uiPresented),currentDisplay.ui?currentDisplay.file:"");receipt(currentDisplay.id,true,"unchanged");if(currentDisplay.scene)delete currentDisplay.scene;currentDisplay={};displayActive=false;streaming=false;commitRequested=false;copy(displayState,sizeof(displayState),"unchanged");return;}
      if(panel.commit(uptime())){vfs.release(currentDisplay.id);copy(displayState,sizeof(displayState),"presenting");frameValid=false;}
    }
  }
  if(panel.phase()==E6Driver::Phase::Completed){copy(frameHash,sizeof(frameHash),pendingHash);frameProfile=currentDisplay.accurate?0xff:6;frameValid=true;finishDisplay(true,"presented","none");}
}

static const char*queueUi(const char*path,const char*driver,bool force,JsonObject out,const char*scope,const UiView*view,PresentationState::Stamp stamp){
 if(shutdownRequested)return "shutdown_in_progress";
 JsonDocument cfg;policies.resolve("/dev/display",scope,cfg.to<JsonObject>());const char*strategy=cfg["pending"]|"latest";
 if((displayActive||streaming)&&!strcmp(strategy,"reject"))return "display_busy";
 if(!strcmp(strategy,"latest"))while(pendingCount){auto&old=pendingDisplays[--pendingCount];receipt(old.id,false,"superseded","newer_presentation");vfs.release(old.id);delete old.scene;old={};}
 if(pendingCount>=unsigned(cfg["queue_limit"]|2))return "display_queue_full";
 Presentation j;j.stamp=stamp;j.latestOnly=!strcmp(strategy,"latest");if(view)j.view=*view;j.id=nextJob++;j.ui=true;j.accurate=!strcmp(driver,"e6.ink6.image-v1");j.force=force;j.timeout=cfg["timeout_ms"]|60000;j.deduplicate=cfg["deduplicate"]|true;copy(j.file,sizeof(j.file),path);rememberJob(j.id);pendingDisplays[pendingCount++]=j;out["job_id"]=j.id;out["status"]="queued";return nullptr;
}
