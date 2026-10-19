// Shared ESP-IDF SPI LCD presentation binding. The composition initializes
// lcdPanel/lcdIo and declares lcdComponent plus its electrical lifecycle.
// Keep the SPI/DMA chain independent of event-loop polling. One immutable
// frame and one small internal DMA buffer remain leased until the final IRQ.
// A larger raster quantum reduces repeated LVGL layout work on full-frame LCDs
// while still yielding between chunks.  A composition can choose its own
// value; the portable default keeps small targets responsive.
#ifndef WISHOS_LCD_RASTER_ROWS
#define WISHOS_LCD_RASTER_ROWS 8
#endif
static constexpr unsigned LCD_RASTER_ROWS = WISHOS_LCD_RASTER_ROWS;
// Rendering yields and native transfer chunks have independent memory needs.
static constexpr unsigned LCD_DMA_ROWS = drivers::LcdDmaRows;
static_assert(LCD_RASTER_ROWS>0,"LCD raster quantum must be positive");
struct LcdTransfer {const uint16_t*frame=nullptr;uint8_t*dma=nullptr;char error[80]{};uint32_t elapsed=0;};
static LcdTransfer lcdTransfer;static bool lcdContentValid=false;
static NativeOperation lcdTransmission(core,nullptr,{[](void(*run)(void*),void*p){return drivers::LcdNativeExecutor::launch(run,p,"wish-lcd-tx",4096);},6144},&lcdTransfer,[](void*p,const std::atomic<bool>&cancel)->const char*{
 auto&t=*static_cast<LcdTransfer*>(p);t.error[0]=0;t.elapsed=0;if(cancel){copy(t.error,sizeof t.error,"cancelled");return t.error;}auto began=uptime();
 for(unsigned y=0;y<boardpack::DISPLAY_HEIGHT;y+=LCD_DMA_ROWS){
  unsigned rows=std::min(LCD_DMA_ROWS,boardpack::DISPLAY_HEIGHT-y);
  for(unsigned i=0;i<boardpack::DISPLAY_WIDTH*rows;++i){auto pixel=t.frame[y*boardpack::DISPLAY_WIDTH+i];t.dma[2*i]=pixel>>8;t.dma[2*i+1]=pixel;}
  lcdCompletion.prepare();auto e=esp_lcd_panel_draw_bitmap(lcdPanel,0,y,boardpack::DISPLAY_WIDTH,y+rows,t.dma);
  if(e!=ESP_OK){copy(t.error,sizeof t.error,esp_err_to_name(e));return t.error;}
  lcdCompletion.wait();
 }
 t.elapsed=uptime()-began;return nullptr;
});
struct LcdPresentation:DisplayRequest{UiView*view=nullptr;char file[256]{};uint16_t*frame=nullptr;uint8_t*dma=nullptr;uint32_t layoutMs=0,rasterMs=0,transferMs=0;unsigned rasterRow=0;bool transmitting=false;};
struct LcdBinding{
 DisplayBegin readiness(){return lcdInitialization.active()?DisplayBegin::deferred():lcdReady?DisplayBegin{}:DisplayBegin::failed(*lcdInitError?lcdInitError:"display_not_initialized");}
 DisplayBegin begin(LcdPresentation&j,uint64_t){
  if(!vfs.lockRead(j.file,j.id))return DisplayBegin::failed("asset_lease_capacity");uiLeaseOwner=j.id;
  j.frame=static_cast<uint16_t*>(core.resources().allocate(boardpack::DISPLAY_WIDTH*boardpack::DISPLAY_HEIGHT*2));j.dma=static_cast<uint8_t*>(core.resources().allocate(boardpack::DISPLAY_WIDTH*LCD_DMA_ROWS*2,ResourceBudget::Region::Internal));
  if(!j.frame||!j.dma){lv_image_cache_drop(nullptr);reapVectorFonts(false,0);return DisplayBegin::deferred();}
  auto layoutAt=uptime();if(!prepareUi(j.file,*j.view))return Core::containsMethod("ui_memory_budget,resource_busy,bus_busy,spi_busy",uiError)?DisplayBegin::deferred():DisplayBegin::failed(uiError);j.layoutMs=uptime()-layoutAt;
  j.rasterRow=0;j.transmitting=false;j.rasterMs=0;lcdTransfer.error[0]=0;
  return {};
 }
 DisplayOutcome poll(LcdPresentation&j,uint64_t){
  if(!j.transmitting){
  const unsigned rotation=uiRenderedView.rotation;
  drivers::LcdRasterFrame raster(j.frame,boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT,rotation);
  UiSink sink{&raster,[](void*ctx,const lv_area_t&a,const uint16_t*pixels){static_cast<drivers::LcdRasterFrame*>(ctx)->write(a.x1,a.y1,a.x2-a.x1+1,a.y2-a.y1+1,pixels);}};
  int w=rotation%180?boardpack::DISPLAY_HEIGHT:boardpack::DISPLAY_WIDTH,h=rotation%180?boardpack::DISPLAY_WIDTH:boardpack::DISPLAY_HEIGHT;
   if(j.rasterRow<unsigned(h)){
    auto began=uptime();unsigned end=std::min(j.rasterRow+LCD_RASTER_ROWS,unsigned(h));
    if(!renderUiRegion({0,int32_t(j.rasterRow),w-1,int32_t(end)-1},sink,true)){
     if(Core::containsMethod("ui_memory_budget,resource_busy,bus_busy,spi_busy",uiError)&&uptime()<j.prepareDeadline)return {};
     return DisplayOutcome::failed(uiError);
    }
    j.rasterMs+=uptime()-began;j.rasterRow=end;if(end<unsigned(h))return {};
   }
   lcdTransfer.frame=j.frame;lcdTransfer.dma=j.dma;lcdTransfer.elapsed=0;JsonDocument started;
   if(auto e=lcdTransmission.start("transmit",started.to<JsonObject>())){
    if(Core::containsMethod("native_operation_memory_budget,native_task_allocation_failed",e)&&uptime()<j.prepareDeadline)return {};
    return DisplayOutcome::failed(e);
   }
   j.transmitting=true;
  }
  lcdTransmission.poll();if(lcdTransmission.active())return {};j.transferMs=lcdTransfer.elapsed;
  return *lcdTransfer.error?DisplayOutcome::failed(lcdTransfer.error):DisplayOutcome::success(DisplayEvidence::TransportComplete,"spi_dma");
 }
 void suspend(LcdPresentation&j){core.resources().freeAllocation(j.frame);core.resources().freeAllocation(j.dma);j.frame=nullptr;j.dma=nullptr;vfs.release(j.id);uiLeaseOwner=0;}
 void completed(LcdPresentation&j,const DisplayOutcome&r,uint64_t elapsed){vfs.release(j.id);uiLeaseOwner=0;if(r.ok()){lcdContentValid=true;
#ifdef WISHOS_LCD_BACKLIGHT_RESOURCE
 backlight.frameReady();backlight.poll(policies);
#endif
 copy(uiPresented,sizeof uiPresented,j.file);commitUiGeometry(j.stamp);}auto&receipt=jobs[j.id%8];if(receipt.id==j.id){receipt.complete=true;receipt.ok=r.ok();copy(receipt.state,sizeof receipt.state,r.state());copy(receipt.error,sizeof receipt.error,r.error);}JsonDocument event;r.describe(j,event.to<JsonObject>(),elapsed);event["timings"]["layout_ms"]=j.layoutMs;event["timings"]["raster_ms"]=j.rasterMs;event["timings"]["transfer_ms"]=j.transferMs;core.emit("/dev/display",r.ok()?"completed":"failed",event.as<JsonObjectConst>(),j.owner);if(!presentationState.active())resetUiRenderer();else reapOrphanFonts();}
 void dispose(LcdPresentation&j){core.resources().freeAllocation(j.frame);core.resources().freeAllocation(j.dma);if(j.view){j.view->~UiView();core.resources().freeAllocation(j.view);}j={};}
};
static LcdBinding lcdBinding;
static DisplayScheduler<LcdPresentation,LcdBinding> lcdScheduler(presentationState,lcdBinding);
static const char*defaultUiDriver(){return "lcd.rgb565.v1";}
static const char*validateUiDriver(const char*driver){return strcmp(driver,defaultUiDriver())?"unsupported_display_driver":nullptr;}
static DisplayAdmission queueUi(const char*path,const char*driver,bool,JsonObject out,const char*scope,const UiView*view,PresentationState::Stamp stamp){
 if(auto e=validateUiDriver(driver))return DisplayAdmission::rejected(e);LcdPresentation j;j.id=nextJob++;j.stamp=stamp;j.owner=stamp.view?presentationState.get(stamp.view)->owner:0;copy(j.file,sizeof j.file,path);auto memory=core.resources().allocate(sizeof(UiView));if(!memory)return DisplayAdmission::deferred("ui_memory_budget");j.view=new(memory)UiView(view?*view:UiView{});
 JsonDocument cfg;policies.resolve("/dev/display",scope,cfg.to<JsonObject>());j.prepareDeadline=uptime()+(cfg["prepare_timeout_ms"]|30000u);using P=decltype(lcdScheduler)::Policy;const char*strategy=cfg["pending"]|"latest";auto result=lcdScheduler.enqueue(j,!strcmp(strategy,"latest")?P::Latest:!strcmp(strategy,"fifo")?P::Fifo:P::Reject,cfg["queue_limit"]|2u);if(result.error){lcdBinding.dispose(j);return result;}rememberJob(j.id);out["job_id"]=j.id;out["status"]="queued";return {};
}
static const char*lcdHandler(const char*m,JsonObjectConst a,JsonObject o){if(strcmp(m,"read")&&strcmp(m,"drivers"))return "unsupported_method";if(auto e=argumentFields(a,!strcmp(m,"drivers")?"driver":""))return e;if(a.containsKey("driver")){if(!a["driver"].is<const char*>())return "driver_id_type";if(auto e=validateUiDriver(a["driver"]))return e;}lcdComponent.describe(o["component"].to<JsonObject>());o["ready"]=lcdReady;o["content_valid"]=lcdContentValid;o["initialization_error"]=lcdInitError;o["active"]=lcdScheduler.active();o["pending"]=lcdScheduler.pending();auto p=o["drivers"].to<JsonArray>().add<JsonObject>();p["id"]=defaultUiDriver();p["color_space"]="rgb565";p["color_count"]=65536;p["pixel_format"]="rgb565";p["width"]=boardpack::DISPLAY_WIDTH;p["height"]=boardpack::DISPLAY_HEIGHT;p["estimated_present_ms"]=400;p["recommended_for"]="General UI and images through the shared UI renderer";p["ui_input"]="brookesia-json-ui-0.1.x-subset";p["completion"]= "transport_complete";
#ifdef WISHOS_LCD_BACKLIGHT_RESOURCE
 o["backlight_resource"]=WISHOS_LCD_BACKLIGHT_RESOURCE;p["backlight_resource"]=WISHOS_LCD_BACKLIGHT_RESOURCE;
#endif
 return nullptr;}
