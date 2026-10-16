// Rendering is shared WishOS UI; ST7789 encoding, transport and completion are
// native. The callback only signals DMA completion and never dispatches apps.
static esp_lcd_panel_handle_t lcdPanel=nullptr;
static esp_lcd_panel_io_handle_t lcdIo=nullptr;
static std::atomic<bool> lcdDmaDone{false};
static bool lcdReady=false;
static char lcdInitError[80]{};
struct LcdTask{void(*run)(void*);void*context;};
static void lcdTask(void*p){auto entry=*static_cast<LcdTask*>(p);delete static_cast<LcdTask*>(p);entry.run(entry.context);vTaskDelete(nullptr);}
static NativeOperation lcdInitialization(core,"/dev/display",{[](void(*run)(void*),void*p){auto entry=new(std::nothrow)LcdTask{run,p};if(!entry)return false;if(xTaskCreate(lcdTask,"wish-lcd-init",8192,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},12288},nullptr,[](void*,const std::atomic<bool>&)->const char*{
 if(auto error=expander.configure(7,7,5)){copy(lcdInitError,sizeof lcdInitError,error);return error;}
 // This device uses an active-low backlight transistor.
 gpio_set_level(gpio_num_t(pins::LCD_BL),1);pinMode(pins::LCD_BL,OUTPUT);
 spi_bus_config_t bus{};bus.sclk_io_num=pins::LCD_SCLK;bus.mosi_io_num=pins::LCD_MOSI;bus.miso_io_num=-1;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=boardpack::DISPLAY_WIDTH*8*2;
 esp_err_t e=spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO);bool busReady=e==ESP_OK;
 if(e==ESP_OK){esp_lcd_panel_io_spi_config_t io{};io.dc_gpio_num=pins::LCD_DC;io.cs_gpio_num=-1;io.pclk_hz=pins::LCD_SPI_HZ;io.lcd_cmd_bits=8;io.lcd_param_bits=8;io.spi_mode=2;io.trans_queue_depth=1;io.on_color_trans_done=[](esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*)->bool{lcdDmaDone.store(true,std::memory_order_release);return false;};e=esp_lcd_new_panel_io_spi(SPI3_HOST,&io,&lcdIo);}
 if(e==ESP_OK){esp_lcd_panel_dev_config_t config{};config.reset_gpio_num=-1;config.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;config.bits_per_pixel=16;e=esp_lcd_new_panel_st7789(lcdIo,&config,&lcdPanel);}
 if(e==ESP_OK)e=esp_lcd_panel_reset(lcdPanel);
 if(e==ESP_OK){if(auto error=expander.write(1,0)){copy(lcdInitError,sizeof lcdInitError,error);e=ESP_FAIL;}}
 if(e==ESP_OK)e=esp_lcd_panel_init(lcdPanel);if(e==ESP_OK)e=esp_lcd_panel_invert_color(lcdPanel,true);if(e==ESP_OK)e=esp_lcd_panel_swap_xy(lcdPanel,true);if(e==ESP_OK)e=esp_lcd_panel_mirror(lcdPanel,true,false);if(e==ESP_OK)e=esp_lcd_panel_disp_on_off(lcdPanel,true);
 if(e!=ESP_OK){if(lcdPanel)esp_lcd_panel_del(lcdPanel);if(lcdIo)esp_lcd_panel_io_del(lcdIo);lcdPanel=nullptr;lcdIo=nullptr;if(busReady)spi_bus_free(SPI3_HOST);if(!*lcdInitError)copy(lcdInitError,sizeof lcdInitError,esp_err_to_name(e));return lcdInitError;}
 return nullptr;
},[](void*){lcdReady=lcdPanel&&!*lcdInitError;if(lcdReady)digitalWrite(pins::LCD_BL,LOW);});
static void startLcdInit(){JsonDocument result;if(auto e=lcdInitialization.start("initialize",result.to<JsonObject>()))copy(lcdInitError,sizeof lcdInitError,e);}
// Keep the SPI/DMA chain independent of event-loop polling. One immutable
// frame and one small internal DMA buffer remain leased until the final IRQ.
struct LcdTransfer {const uint16_t*frame=nullptr;uint8_t*dma=nullptr;char error[80]{};uint32_t elapsed=0;};
static LcdTransfer lcdTransfer;
static NativeOperation lcdTransmission(core,nullptr,{[](void(*run)(void*),void*p){auto entry=new(std::nothrow)LcdTask{run,p};if(!entry)return false;if(xTaskCreate(lcdTask,"wish-lcd-tx",4096,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},6144},&lcdTransfer,[](void*p,const std::atomic<bool>&cancel)->const char*{
 auto&t=*static_cast<LcdTransfer*>(p);t.error[0]=0;t.elapsed=0;if(cancel){copy(t.error,sizeof t.error,"cancelled");return t.error;}auto began=uptime();
 for(unsigned y=0;y<boardpack::DISPLAY_HEIGHT;y+=8){
  unsigned rows=std::min(8u,boardpack::DISPLAY_HEIGHT-y);
  for(unsigned i=0;i<boardpack::DISPLAY_WIDTH*rows;++i){auto pixel=t.frame[y*boardpack::DISPLAY_WIDTH+i];t.dma[2*i]=pixel>>8;t.dma[2*i+1]=pixel;}
  lcdDmaDone.store(false,std::memory_order_release);auto e=esp_lcd_panel_draw_bitmap(lcdPanel,0,y,boardpack::DISPLAY_WIDTH,y+rows,t.dma);
  if(e!=ESP_OK){copy(t.error,sizeof t.error,esp_err_to_name(e));return t.error;}
  while(!lcdDmaDone.load(std::memory_order_acquire))vTaskDelay(1);
 }
 t.elapsed=uptime()-began;return nullptr;
});
struct LcdPresentation:DisplayRequest{UiView*view=nullptr;char file[256]{};uint16_t*frame=nullptr;uint8_t*dma=nullptr;uint32_t layoutMs=0,rasterMs=0,transferMs=0;};
static constexpr DisplayComponent lcdComponent{"st7789","lcd","spi",320,240,DisplayEvidence::TransportComplete,"spi_dma","on_demand"};
struct LcdBinding{
 DisplayBegin readiness(){return lcdInitialization.active()?DisplayBegin::deferred():lcdReady?DisplayBegin{}:DisplayBegin::failed(*lcdInitError?lcdInitError:"display_not_initialized");}
 DisplayBegin begin(LcdPresentation&j,uint64_t){
  if(!vfs.lockRead(j.file,j.id))return DisplayBegin::failed("asset_lease_capacity");uiLeaseOwner=j.id;
  j.frame=static_cast<uint16_t*>(core.resources().allocate(boardpack::DISPLAY_WIDTH*boardpack::DISPLAY_HEIGHT*2));j.dma=static_cast<uint8_t*>(core.resources().allocate(boardpack::DISPLAY_WIDTH*8*2,ResourceBudget::Region::Internal));
  if(!j.frame||!j.dma)return DisplayBegin::deferred();
  auto layoutAt=uptime();if(!prepareUi(j.file,*j.view))return Core::containsMethod("ui_memory_budget,resource_busy,bus_busy,spi_busy",uiError)?DisplayBegin::deferred():DisplayBegin::failed(uiError);j.layoutMs=uptime()-layoutAt;
  UiSink sink{j.frame,[](void*ctx,const lv_area_t&a,const uint16_t*pixels){auto frame=static_cast<uint16_t*>(ctx);for(int y=a.y1;y<=a.y2;++y)for(int x=a.x1;x<=a.x2;++x){int px=x,py=y;switch(uiRenderedView.rotation){case 90:px=boardpack::DISPLAY_WIDTH-1-y;py=x;break;case 180:px=boardpack::DISPLAY_WIDTH-1-x;py=boardpack::DISPLAY_HEIGHT-1-y;break;case 270:px=y;py=boardpack::DISPLAY_HEIGHT-1-x;break;}frame[py*boardpack::DISPLAY_WIDTH+px]=pixels[(y-a.y1)*(a.x2-a.x1+1)+x-a.x1];}}};
  int w=uiRenderedView.rotation%180?boardpack::DISPLAY_HEIGHT:boardpack::DISPLAY_WIDTH,h=uiRenderedView.rotation%180?boardpack::DISPLAY_WIDTH:boardpack::DISPLAY_HEIGHT;
  auto rasterAt=uptime();if(!renderUiRegion({0,0,w-1,h-1},sink))return Core::containsMethod("ui_memory_budget,resource_busy,bus_busy,spi_busy",uiError)?DisplayBegin::deferred():DisplayBegin::failed(uiError);j.rasterMs=uptime()-rasterAt;
  lcdTransfer.frame=j.frame;lcdTransfer.dma=j.dma;lcdTransfer.elapsed=0;JsonDocument started;
  if(auto e=lcdTransmission.start("transmit",started.to<JsonObject>()))return Core::containsMethod("native_operation_memory_budget,native_task_allocation_failed",e)?DisplayBegin::deferred():DisplayBegin::failed(e);
  return {};
 }
 DisplayOutcome poll(LcdPresentation&j,uint64_t){
  lcdTransmission.poll();if(lcdTransmission.active())return {};j.transferMs=lcdTransfer.elapsed;
  return *lcdTransfer.error?DisplayOutcome::failed(lcdTransfer.error):DisplayOutcome::success(DisplayEvidence::TransportComplete,"spi_dma");
 }
 void suspend(LcdPresentation&j){core.resources().freeAllocation(j.frame);core.resources().freeAllocation(j.dma);j.frame=nullptr;j.dma=nullptr;vfs.release(j.id);uiLeaseOwner=0;}
 void completed(LcdPresentation&j,const DisplayOutcome&r,uint64_t elapsed){vfs.release(j.id);uiLeaseOwner=0;if(r.ok()){copy(uiPresented,sizeof uiPresented,j.file);commitUiGeometry(j.stamp);}auto&receipt=jobs[j.id%8];if(receipt.id==j.id){receipt.complete=true;receipt.ok=r.ok();copy(receipt.state,sizeof receipt.state,r.state());copy(receipt.error,sizeof receipt.error,r.error);}JsonDocument event;r.describe(j,event.to<JsonObject>(),elapsed);event["timings"]["layout_ms"]=j.layoutMs;event["timings"]["raster_ms"]=j.rasterMs;event["timings"]["transfer_ms"]=j.transferMs;core.emit("/dev/display",r.ok()?"completed":"failed",event.as<JsonObjectConst>(),j.owner);if(!presentationState.active())resetUiRenderer();else reapOrphanFonts();}
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
static const char*lcdHandler(const char*m,JsonObjectConst a,JsonObject o){if(strcmp(m,"read")&&strcmp(m,"drivers"))return "unsupported_method";if(auto e=argumentFields(a,!strcmp(m,"drivers")?"driver":""))return e;if(a.containsKey("driver")){if(!a["driver"].is<const char*>())return "driver_id_type";if(auto e=validateUiDriver(a["driver"]))return e;}lcdComponent.describe(o["component"].to<JsonObject>());o["ready"]=lcdReady;o["initialization_error"]=lcdInitError;o["active"]=lcdScheduler.active();o["pending"]=lcdScheduler.pending();auto p=o["drivers"].to<JsonArray>().add<JsonObject>();p["id"]=defaultUiDriver();p["color_space"]="rgb565";p["color_count"]=65536;p["pixel_format"]="rgb565";p["width"]=boardpack::DISPLAY_WIDTH;p["height"]=boardpack::DISPLAY_HEIGHT;p["estimated_present_ms"]=400;p["recommended_for"]="General UI and images through the shared UI renderer";p["ui_input"]="brookesia-json-ui-0.1.x-subset";p["completion"]= "transport_complete";return nullptr;}
