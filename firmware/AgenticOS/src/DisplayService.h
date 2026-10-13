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
struct Presentation {
 PresentationState::Stamp stamp;UiView view;uint32_t id=0;
 E6Profile profile=E6Profile::ColorFast;nm::Scene*scene=nullptr;char file[256]{};
 bool latestOnly=true,maintenance=false,shutdown=false,force=false,ui=false,deduplicate=true,started=false;
 uint32_t timeout=60000,streamTimeout=15000;
};
static Presentation pendingDisplays[4],currentDisplay;
static unsigned pendingCount=0;static nm::Stripe renderStripe;
static char pendingHash[65]{};static bool commitRequested=false,hashLive=false,hashingColor=false;
static unsigned hashPlane=0;static uint64_t displayBegan=0;static uint32_t renderedBytes=0;
static const char*defaultUiDriver(){return "e6.color-fast.v1";}
static const char*validateUiDriver(const char*id){return e6Profile(id)?nullptr:"unsupported_display_driver";}
static bool presentationCurrent(const Presentation&job){if(!job.stamp.view)return job.shutdown||job.maintenance;auto now=presentationState.stamp(job.stamp.view);return now.activation&&now.activation==job.stamp.activation&&(!job.latestOnly||now.revision==job.stamp.revision);}
static void receipt(uint32_t id,bool ok,const char*state,const char*error="none",uint32_t ms=0){
 auto&j=jobs[id%8];if(j.id!=id)return;j.complete=true;j.ok=ok;j.total=ms;copy(j.state,sizeof j.state,state);copy(j.error,sizeof j.error,error);
 JsonDocument d;auto o=d.to<JsonObject>();if(currentDisplay.id==id){o["view_id"]=currentDisplay.stamp.view;o["view_revision"]=currentDisplay.stamp.revision;o["activation"]=currentDisplay.stamp.activation;}
 o["job_id"]=id;o["state"]=state;o["success"]=ok;o["error"]=error;o["total_ms"]=ms;emit("/dev/display",ok?"completed":"failed",o);
}
static void resetDigest(){if(hashLive)mbedtls_sha256_free(&digest);mbedtls_sha256_init(&digest);mbedtls_sha256_starts(&digest,0);hashLive=true;}
static void finishDisplay(bool ok,const char*state,const char*error){
 vfs.release(currentDisplay.id);receipt(currentDisplay.id,ok,state,error,uint32_t(uptime()-displayBegan));
 if(ok){if(currentDisplay.stamp.view)presentationState.completed(currentDisplay.stamp);presentedJob=currentDisplay.id;copy(uiPresented,sizeof uiPresented,currentDisplay.ui?currentDisplay.file:"");}
 if(!ok&&currentDisplay.started){frameValid=false;colorPanel.invalidate();monoCache.clear();}
 if(ok&&strcmp(state,"unchanged")){lastRefreshRTC=esp_clk_rtc_time();lastClear=millis();}
 if(ok&&currentDisplay.profile!=E6Profile::BlueYellow)monoCache.clear();
 bool cut=currentDisplay.shutdown;delete currentDisplay.scene;currentDisplay={};displayActive=false;streaming=false;hashingColor=false;commitRequested=false;
 if(hashLive){mbedtls_sha256_free(&digest);hashLive=false;}
 copy(displayState,sizeof displayState,state);copy(displayError,sizeof displayError,error);if(cut){if(ok)shutdownReady=true;else shutdownRequested=false;}
}
static bool unchanged(){return !currentDisplay.force&&currentDisplay.deduplicate&&frameValid&&frameProfile==uint8_t(currentDisplay.profile)&&!strcmp(frameHash,pendingHash)&&esp_clk_rtc_time()-lastRefreshRTC<86220000000ULL;}
static const char*readDisplayBand(E6ColorDriver::Frame frame,size_t at,size_t&bytes){
 bytes=5760;
 if(frame==E6ColorDriver::Frame::Zero||frame==E6ColorDriver::Frame::MonoReturn){memset(renderStripe.bytes,0,bytes);return nullptr;}
 bool low=frame==E6ColorDriver::Frame::Low;
 if(currentDisplay.ui)return renderUiBand(at/5760*16,renderStripe,currentDisplay.profile,low)?nullptr:uiError;
 if(currentDisplay.scene){
  renderStripe.startColumns(at/5760*16,currentDisplay.scene->background);renderStripe.render(*currentDisplay.scene);
  if(e6Extended(currentDisplay.profile)){
   static constexpr uint16_t rgb[]={0,0xffff,0xffe0,0xf800,0,0x001f,0x07e0};
   for(auto&b:renderStripe.bytes)b=(e6Plane(e6Ink(rgb[b>>4],currentDisplay.profile),currentDisplay.profile,low)<<4)|e6Plane(e6Ink(rgb[b&15],currentDisplay.profile),currentDisplay.profile,low);
  }return nullptr;
 }
 const char*relative;auto provider=vfs.resolve(currentDisplay.file,relative);if(!provider)return "source_removed";size_t got=0;
 if(e6Color(currentDisplay.profile)){
  bytes=2880;if(auto e=provider->read(relative,at*2,renderStripe.bytes,bytes*2,got))return e;if(got!=bytes*2)return "source_short_read";
  for(size_t i=0;i<bytes;++i){uint8_t a=renderStripe.bytes[2*i],b=renderStripe.bytes[2*i+1];if(!e6ValidCode(currentDisplay.profile,a)||!e6ValidCode(currentDisplay.profile,b))return "invalid_palette_code";renderStripe.bytes[i]=(e6Plane(a,currentDisplay.profile,low)<<4)|e6Plane(b,currentDisplay.profile,low);}
 }else if(currentDisplay.profile==E6Profile::BlueYellow){
  bytes=2880;if(auto e=provider->read(relative,at/4,renderStripe.bytes,bytes/4,got))return e;if(got!=bytes/4)return "source_short_read";
  for(size_t i=bytes;i--;){size_t bit=i*2;uint8_t source=renderStripe.bytes[bit/8];renderStripe.bytes[i]=(((source>>(7-bit%8))&1)<<4)|((source>>(6-bit%8))&1);}
 }else{
  if(auto e=provider->read(relative,at,renderStripe.bytes,bytes,got))return e;if(got!=bytes)return "source_short_read";
  for(size_t i=0;i<bytes;++i)if(!e6ValidCode(currentDisplay.profile,renderStripe.bytes[i]>>4)||!e6ValidCode(currentDisplay.profile,renderStripe.bytes[i]&15))return "invalid_native_colors";
 }return nullptr;
}
static const char*beginCurrent(){
 displayBegan=uptime();applyPanelClockIfIdle();if(currentDisplay.file[0]&&!vfs.lockRead(currentDisplay.file,currentDisplay.id))return "asset_lease_capacity";
 uiLeaseOwner=currentDisplay.id;if(currentDisplay.ui&&!prepareUi(currentDisplay.file,currentDisplay.view))return uiError;
 displayBegan=uptime();renderedBytes=0;hashPlane=0;resetDigest();
 if(e6Extended(currentDisplay.profile)){hashingColor=true;displayActive=true;copy(displayState,sizeof displayState,"preparing");return nullptr;}
 if(!takeSPI())return "spi_busy";auto e=ensureRail();giveSPI();if(e)return e;colorPanel.invalidate();monoCache.clear();
 if(!panel.start(currentDisplay.profile==E6Profile::StockImage,uptime(),currentDisplay.timeout))return "display_not_ready";
 currentDisplay.started=true;displayActive=true;copy(displayState,sizeof displayState,"preparing");return nullptr;
}
static const char*admitPresentation(Presentation&j,JsonObject out,const char*scope){
 if(shutdownRequested)return "shutdown_in_progress";
 JsonDocument cfg;policies.resolve("/dev/display",scope,cfg.to<JsonObject>());const char*strategy=cfg["pending"]|"latest";
 if((displayActive||streaming)&&!strcmp(strategy,"reject"))return "display_busy";
 if(!strcmp(strategy,"latest"))while(pendingCount){auto&old=pendingDisplays[--pendingCount];receipt(old.id,false,"superseded","newer_presentation");vfs.release(old.id);delete old.scene;old={};}
 if(pendingCount>=unsigned(cfg["queue_limit"]|2))return "display_queue_full";
 j.latestOnly=!strcmp(strategy,"latest");j.id=nextJob++;j.timeout=cfg["timeout_ms"]|60000;j.deduplicate=cfg["deduplicate"]|true;
 rememberJob(j.id);pendingDisplays[pendingCount++]=j;out["job_id"]=j.id;out["status"]="queued";return nullptr;
}
static const char*queueDisplay(nm::Payload*payload,E6Profile profile,bool shutdown,JsonObject out,const char*scope=appScope,PresentationState::Stamp stamp={}){
 auto scene=new(std::nothrow)nm::Scene;if(!scene){delete payload;return "memory_low";}
 if(!nm::validate(*payload,*scene)){delete scene;delete payload;return "invalid_scene";}delete payload;
 Presentation j;j.stamp=stamp;j.scene=scene;j.profile=profile;j.shutdown=shutdown;j.force=shutdown;
 if(auto e=admitPresentation(j,out,scope)){delete scene;return e;}return nullptr;
}
static const char*queueUi(const char*path,const char*driver,bool force,JsonObject out,const char*scope,const UiView*view,PresentationState::Stamp stamp){
 auto profile=e6Profile(driver);if(!profile)return "unsupported_display_driver";
 Presentation j;j.stamp=stamp;j.profile=profile->profile;j.ui=true;j.force=force;if(view)j.view=*view;copy(j.file,sizeof j.file,path);return admitPresentation(j,out,scope);
}
static void drivers(JsonArray arr,const char*selected=""){
 static constexpr uint8_t stockCodes[]={0,1,2,3,5,6};static constexpr const char*stockRGB[]={"000000","ffffff","ffff00","ff0000","0000ff","00ff00"};
 for(auto&p:E6_PROFILES){if(*selected&&strcmp(selected,p.id))continue;auto d=arr.add<JsonObject>();d["id"]=p.id;d["color_space"]=p.id;d["color_count"]=p.colors;d["width"]=480;d["height"]=720;d["input_format"]=p.format;d["frame_bytes"]=p.sourceBytes;d["estimated_present_ms"]=p.estimateMs;d["recommended_for"]=p.guide;d["palette_calibrated"]=false;
  d["streaming"]=!e6Extended(p.profile);d["source_access"]=e6Extended(p.profile)?"replayable_ui_scene_or_file":"stream_or_file";d["cancel_while_presenting"]=false;d["timing_source"]="previous_panel_observation";d["validated_on_this_board"]=false;
  if(e6Extended(p.profile)){d["cold_prepare_extra_ms"]=3200;d["mode_transition_internal"]=true;}
  if(p.profile==E6Profile::BlueYellow){d["old_pixel_cache"]="lossless_band_rle_or_raw";d["cache_max_payload_bytes"]=43200;}
  auto codes=d["colors"].to<JsonArray>(),palette=d["palette_rgb"].to<JsonArray>();
  for(unsigned i=0;i<p.colors;++i){if(!e6Extended(p.profile)){codes.add(stockCodes[i]);palette.add(stockRGB[i]);}else if(p.profile==E6Profile::BlueYellow){codes.add(i);palette.add(i?"0000ff":"ffff00");}else{uint8_t code=p.profile==E6Profile::ColorFast?e6::tickey_fast_valid_codes[i]:e6::tickey_qual_valid_codes[i];const auto*rgb=p.profile==E6Profile::ColorFast?e6::tickey_fast_rgb64[code]:e6::tickey_qual_rgb64[code];char text[7];snprintf(text,sizeof text,"%02x%02x%02x",rgb[0],rgb[1],rgb[2]);codes.add(code);palette.add(text);}}
 }
}
static const char*validateDisplayArguments(const char*m,JsonObjectConst a){
 const char*allowed=!strcmp(m,"drivers")?"driver":Policies::choice("read,recover",m)?"":!strcmp(m,"chunk")?"stream_id,offset,hex":!strcmp(m,"commit")?"stream_id,sha256":!strcmp(m,"abort")?"stream_id":"view_id,view_revision,driver,scope,force,file,scene";
 for(auto f:a)if(!Policies::choice(allowed,f.key().c_str()))return "unknown_display_argument";
 if(a.containsKey("file")&&strcmp(m,"present"))return "unknown_display_argument";if(a.containsKey("scene")&&strcmp(m,"scene"))return "unknown_display_argument";
 if(a.containsKey("sha256")&&!a["sha256"].isNull()&&(!a["sha256"].is<const char*>()||strlen(a["sha256"].as<const char*>())!=64))return "hash_format";
 for(auto key:{"driver","scope","file","hex"})if(a.containsKey(key)&&!a[key].is<const char*>())return "display_argument_type";
 for(auto key:{"view_id","view_revision","stream_id","offset"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "display_argument_type";
 if(a.containsKey("force")&&!a["force"].is<bool>())return "display_argument_type";if(a.containsKey("scope")&&!Policies::validScope(a["scope"]))return "invalid_scope";return nullptr;
}
static const char*displayHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(auto e=validateDisplayArguments(m,a))return e;
 if(!strcmp(m,"drivers")){if(a.containsKey("driver")&&!e6Profile(a["driver"].as<const char*>()))return "unsupported_display_driver";drivers(out["drivers"].to<JsonArray>(),a["driver"]|"");return nullptr;}
 if(!strcmp(m,"read")){out["state"]=(panel.faulted()||colorPanel.faulted())?"fault":displayState;out["ready"]=panelReady();out["active"]=bool(displayActive);out["receiving"]=e6Extended(currentDisplay.profile)?colorPanel.receiving():panel.receiving();out["streaming"]=bool(streaming);out["offset"]=e6Extended(currentDisplay.profile)?colorPanel.bytes():panel.bytes();out["stream_id"]=streamId;out["job_id"]=currentDisplay.id;out["pending"]=pendingCount;out["mono_cache_bytes"]=monoCache.bytes();out["hash"]=frameValid?frameHash:"";out["error"]=colorPanel.faulted()?colorPanel.error():panel.faulted()?panel.error():displayError;return nullptr;}
 if(!strcmp(m,"recover")){bool recovered=colorPanel.faulted()?colorPanel.clearFault(uptime()):panel.clearFault(uptime());if(!recovered)return "display_not_stably_ready";frameValid=false;colorPanel.invalidate();monoCache.clear();copy(displayError,sizeof displayError,"none");copy(displayState,sizeof displayState,"idle");return nullptr;}
 JsonDocument cfg;policies.resolve("/dev/display",a["scope"]|appScope,cfg.to<JsonObject>());const char*id=a["driver"]|(cfg["driver"]|defaultUiDriver());auto info=e6Profile(id);if(!info)return "unsupported_display_driver";
 PresentationState::Stamp stamp;if(Policies::choice("present,scene,clear,begin",m))if(auto e=displayViewStamp(a,stamp))return e;
 if(!strcmp(m,"present")){char full[256];const char*rel;FileProvider*p;if(!resolveFile(a["file"]|"",full,rel,p))return "invalid_file_path";FileInfo file;if(auto e=p->stat(rel,file))return e;if(file.directory||file.size!=info->sourceBytes)return "native_frame_size";auto e=queueUi(full,id,a["force"]|false,out,a["scope"]|appScope,nullptr,stamp);if(!e)pendingDisplays[pendingCount-1].ui=false;return e;}
 if(!strcmp(m,"scene")||!strcmp(m,"clear")){auto p=new(std::nothrow)nm::Payload;if(!p)return "memory_low";if(!strcmp(m,"clear")){const char*s="{\"format\":\"scene-v1\",\"background\":1,\"commands\":[]}";p->size=strlen(s);memcpy(p->bytes,s,p->size);}else{auto s=a["scene"].as<JsonObjectConst>();if(s.isNull()||measureJson(s)>board::MAX_SCENE){delete p;return "scene_limit";}p->size=serializeJson(s,p->bytes,sizeof p->bytes);}auto e=queueDisplay(p,info->profile,false,out,a["scope"]|appScope,stamp);if(!e)pendingDisplays[pendingCount-1].force=a["force"]|false;return e;}
 if(!strcmp(m,"begin")){
  if(e6Extended(info->profile))return "driver_requires_replayable_source";
  if(displayActive||streaming||pendingCount||panel.faulted()||colorPanel.faulted()||shutdownRequested)return "display_busy";
  currentDisplay={};currentDisplay.stamp=stamp;currentDisplay.id=nextJob++;currentDisplay.profile=info->profile;currentDisplay.timeout=cfg["timeout_ms"]|60000;currentDisplay.force=a["force"]|false;currentDisplay.deduplicate=cfg["deduplicate"]|true;currentDisplay.streamTimeout=cfg["stream_timeout_ms"]|15000u;
  rememberJob(currentDisplay.id);if(auto e=beginCurrent()){finishDisplay(false,"failed",e);return e;}streaming=true;streamId=nextStream++;streamDeadline=millis()+currentDisplay.streamTimeout;out["stream_id"]=streamId;out["job_id"]=currentDisplay.id;out["status"]="preparing";out["chunk_limit"]=1024;return nullptr;
 }
 if(!streaming||a["stream_id"].as<uint32_t>()!=streamId)return "stream_not_found";
 if(!presentationCurrent(currentDisplay))return "view_changed";
 if(!strcmp(m,"abort")){if(!panel.receiving())return "not_cancellable";panel.cancel();finishDisplay(false,"cancelled","cancelled");return nullptr;}
 if(!panel.receiving())return "display_not_receiving";
 if(!strcmp(m,"chunk")){uint8_t b[1024];size_t n;if(!a["offset"].is<uint32_t>()||a["offset"].as<uint32_t>()!=panel.bytes())return "wrong_offset";if(!unhex(a["hex"]|"",b,sizeof b,n))return "invalid_hex";if(!panel.write(b,n))return "chunk_not_accepted";mbedtls_sha256_update(&digest,b,n);streamDeadline=millis()+currentDisplay.streamTimeout;out["next_offset"]=panel.bytes();return nullptr;}
 if(panel.bytes()!=E6Driver::FRAME_BYTES)return "incomplete_frame";finishHash(pendingHash);hashLive=false;
 if(!a["sha256"].isNull()&&strcmp(a["sha256"],pendingHash)){panel.cancel();finishDisplay(false,"failed","hash_mismatch");return "hash_mismatch";}streaming=false;commitRequested=true;out["job_id"]=currentDisplay.id;out["status"]="queued";return nullptr;
}
static void pollDisplay(){
 pollUiPresentation();
 while(pendingCount&&!presentationCurrent(pendingDisplays[0])){auto&j=pendingDisplays[0];receipt(j.id,false,"superseded","view_changed");vfs.release(j.id);delete j.scene;for(unsigned i=1;i<pendingCount;++i)pendingDisplays[i-1]=pendingDisplays[i];pendingDisplays[--pendingCount]={};}
 if(!displayActive&&pendingCount&&!panel.faulted()&&!colorPanel.faulted()){
  currentDisplay=pendingDisplays[0];auto e=beginCurrent();
  if(e&&!strcmp(e,"spi_busy")){vfs.release(currentDisplay.id);uiLeaseOwner=0;currentDisplay={};return;}
  for(unsigned i=1;i<pendingCount;++i)pendingDisplays[i-1]=pendingDisplays[i];pendingDisplays[--pendingCount]={};if(e){finishDisplay(false,"failed",e);return;}
 }
 if(!displayActive)return;
 if(e6Extended(currentDisplay.profile)){
  if(hashingColor){
   if(!presentationCurrent(currentDisplay)){finishDisplay(false,"superseded","view_changed");return;}
   size_t bytes=0;auto frame=currentDisplay.profile==E6Profile::BlueYellow?E6ColorDriver::Frame::Mono:hashPlane?E6ColorDriver::Frame::Low:E6ColorDriver::Frame::High;
   if(renderedBytes<E6Driver::FRAME_BYTES){if(auto e=readDisplayBand(frame,renderedBytes,bytes)){if(!strcmp(e,"spi_busy"))return;finishDisplay(false,"failed",e);return;}mbedtls_sha256_update(&digest,renderStripe.bytes,bytes);renderedBytes+=bytes;return;}
   if(e6Color(currentDisplay.profile)&&!hashPlane){hashPlane=1;renderedBytes=0;return;}
   if(hashLive){finishHash(pendingHash);hashLive=false;}
   if(unchanged()){finishDisplay(true,"unchanged","none");return;}
   if(!takeSPI())return;auto error=ensureRail();giveSPI();if(error){finishDisplay(false,"failed",error);return;}
   if(!colorPanel.start(e6Mode(currentDisplay.profile),&monoCache,uptime(),currentDisplay.timeout)){finishDisplay(false,"failed","display_not_ready");return;}
   currentDisplay.started=true;hashingColor=false;frameValid=false;copy(displayState,sizeof displayState,"presenting");
  }
  colorPanel.tick(uptime());
  if(colorPanel.faulted()){finishDisplay(false,"failed",colorPanel.error());return;}
  if(colorPanel.receiving()){size_t bytes=0;if(auto e=readDisplayBand(colorPanel.frame(),colorPanel.bytes(),bytes)){if(!strcmp(e,"spi_busy"))return;colorPanel.sourceFailed(e,uptime());return;}colorPanel.write(renderStripe.bytes,bytes,uptime());}
  if(colorPanel.completed()){copy(frameHash,sizeof frameHash,pendingHash);frameProfile=uint8_t(currentDisplay.profile);frameValid=true;finishDisplay(true,"presented","none");}return;
 }
 panel.tick(uptime());if(panel.faulted()){finishDisplay(false,"failed",panel.error());return;}
 if(panel.receiving()){
  if(!presentationCurrent(currentDisplay)){panel.cancel();finishDisplay(false,"superseded","view_changed");return;}copy(displayState,sizeof displayState,"receiving");
  if(!streaming&&!commitRequested){size_t bytes=0;if(auto e=readDisplayBand(E6ColorDriver::Frame::High,renderedBytes,bytes)){if(!strcmp(e,"spi_busy"))return;panel.cancel();finishDisplay(false,"failed",e);return;}
   if(panel.write(renderStripe.bytes,bytes)){mbedtls_sha256_update(&digest,renderStripe.bytes,bytes);renderedBytes+=bytes;}if(renderedBytes==E6Driver::FRAME_BYTES){finishHash(pendingHash);hashLive=false;commitRequested=true;}
  }
  if(streaming&&int32_t(millis()-streamDeadline)>=0){panel.cancel();finishDisplay(false,"failed","stream_timeout");return;}
  if(commitRequested){if(unchanged()){panel.cancel();finishDisplay(true,"unchanged","none");return;}if(panel.commit(uptime())){copy(displayState,sizeof displayState,"presenting");frameValid=false;}}
 }
 if(panel.phase()==E6Driver::Phase::Completed){copy(frameHash,sizeof frameHash,pendingHash);frameProfile=uint8_t(currentDisplay.profile);frameValid=true;finishDisplay(true,"presented","none");}
}
