static InputPresentation inputPresentation{};
static portMUX_TYPE inputPresentationMux=portMUX_INITIALIZER_UNLOCKED;
static InputPresentation IRAM_ATTR capturePresentation(){portENTER_CRITICAL_ISR(&inputPresentationMux);auto snapshot=inputPresentation;portEXIT_CRITICAL_ISR(&inputPresentationMux);return snapshot;}
static void publishPresentation();
static const char*defaultNfsExport(){return "/";}
static const char*defaultNfsProvider(){return "flash";}
static const char*nativeStorage(const char*m,JsonObjectConst a,JsonObject o){return flashVolume.invokeSelected(m,a,o);}
static void startLcdInit();
static const char*lcdHandler(const char*,JsonObjectConst,JsonObject);
static SystemNavigation navigation(core,applications,1,1,1);
static char appScope[48]="system";
static constexpr Setting SETTINGS[]={
 uiDrawBufferSetting(boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT,"40960"),
 drivers::EspLedcBacklight::setting(),
 Setting{"/dev/battery","sample_interval_ms","integer","30000","",100,3600000}.inSystem().at("next_sample"),
 Setting{"/sys/ui","font_file","string","\"\"","",0,255}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","font_cache_glyphs","integer","128","",0,1024}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","image_cache_bytes","integer","65536","",0,1048576}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","font_face_cache","integer","8","",0,32}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","retain_renderer_on_switch","boolean","true"}.inSystem().at("next_application_exit"),
 {"/dev/display","pending","enum","\"latest\"","latest,fifo,reject"},
 {"/dev/display","prepare_timeout_ms","integer","30000","",1,4294967295.0},
 {"/dev/display","queue_limit","integer","2","",1,4},
 {"/dev/display","driver","enum","\"lcd.rgb565.v1\"","lcd.rgb565.v1"},
 {"/sys/ui","png_decode_budget_bytes","integer","131072","",1024,1048576}
};
static Policies policies(SETTINGS,sizeof SETTINGS/sizeof *SETTINGS,&core.resources());
static drivers::EspLedcBacklight backlight({pins::LCD_BL,pins::BACKLIGHT_ACTIVE_LOW,LEDC_LOW_SPEED_MODE,ledc_timer_t(pins::BACKLIGHT_LEDC_TIMER),ledc_channel_t(pins::BACKLIGHT_LEDC_CHANNEL),unsigned(pins::BACKLIGHT_PWM_HZ),unsigned(pins::BACKLIGHT_PWM_BITS),LEDC_USE_APB_CLK});
static bool resolveFile(const char*path,char*full,const char*&relative,FileProvider*&provider){if(!normalizePath("/",path,full,256))return false;provider=vfs.resolve(full,relative);return provider!=nullptr;}
#include "UiService.h"
static void publishPresentation(){auto s=presentationState.presented();portENTER_CRITICAL(&inputPresentationMux);inputPresentation={s.view,s.revision,s.activation};portEXIT_CRITICAL(&inputPresentationMux);}

#include "Audio.inc"
#include "drivers/esp32-services/BleService.h"
// Native edges retain their actual timestamps; system callbacks stay on the loop.
struct ButtonEdge {uint32_t at,mask;InputPresentation presentation;};
static QueueHandle_t buttonEdges=nullptr;
static std::atomic<uint32_t>inputOverruns{0};
static uint32_t inputRaw=0;static InputPresentation inputRawPresentation{};
static uint32_t IRAM_ATTR buttonMask(){return gpio_get_level(gpio_num_t(pins::KEY_USER))?0:1;}
static void IRAM_ATTR captureButtonEdge(){ButtonEdge e{millis(),buttonMask(),capturePresentation()};BaseType_t wake=pdFALSE;if(xQueueSendFromISR(buttonEdges,&e,&wake)!=pdTRUE)inputOverruns.fetch_add(1,std::memory_order_relaxed);if(wake)portYIELD_FROM_ISR();}
static void buttonEvent(void*,const char*name,JsonObjectConst data){core.emit("/dev/buttons",name,data,0,uptime());}
static ButtonGestures buttonGestures({"USER"},nullptr,buttonEvent);
static void sampleButtons(uint32_t raw,uint64_t at,InputPresentation view){navigation.poll(raw,at);buttonGestures.consumeCurrentPress(navigation.consumedButtons());buttonGestures.sample(0,bool(raw&1),uint32_t(at),view);}
static void pollButtonInput(){
 if(!buttonEdges)return;ButtonEdge e;
 while(xQueueReceive(buttonEdges,&e,0)==pdTRUE){uint64_t now=uptime(),at=now-uint32_t(uint32_t(now)-e.at);sampleButtons(inputRaw,at,inputRawPresentation);inputRaw=e.mask;inputRawPresentation=e.presentation;sampleButtons(inputRaw,at,inputRawPresentation);}
 if(inputOverruns.exchange(0)){inputRaw=buttonMask();inputRawPresentation=capturePresentation();event("/dev/buttons","failed","input_edge_overflow");}
 sampleButtons(inputRaw,uptime(),inputRawPresentation);
}
static esp_lcd_panel_handle_t lcdPanel=nullptr;
static esp_lcd_panel_io_handle_t lcdIo=nullptr;
static drivers::LcdDmaCompletion lcdCompletion;
static bool lcdReady=false,lcdBus=false;
static char lcdInitError[80]{};
static void cleanupLcd(){if(lcdPanel){esp_lcd_panel_del(lcdPanel);lcdPanel=nullptr;}if(lcdIo){esp_lcd_panel_io_del(lcdIo);lcdIo=nullptr;}if(lcdBus){spi_bus_free(SPI3_HOST);lcdBus=false;}}
static NativeOperation lcdInitialization(core,"/dev/display",{[](void(*run)(void*),void*p){return drivers::LcdNativeExecutor::launch(run,p,"wish-lcd-init",8192);},12288},nullptr,[](void*,const std::atomic<bool>&cancel)->const char*{
 if(!lcdCompletion.initialize())return "lcd_completion_init_failed";
 spi_bus_config_t bus{};bus.sclk_io_num=pins::LCD_SCLK;bus.mosi_io_num=pins::LCD_MOSI;bus.miso_io_num=-1;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=boardpack::DISPLAY_WIDTH*drivers::LcdDmaRows*2;
 drivers::lcdSpiInterruptAffinity(bus);
 auto e=spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO);if(e!=ESP_OK)return esp_err_to_name(e);lcdBus=true;
 esp_lcd_panel_io_spi_config_t io{};io.dc_gpio_num=pins::LCD_DC;io.cs_gpio_num=pins::LCD_CS;io.pclk_hz=pins::LCD_SPI_HZ;io.lcd_cmd_bits=8;io.lcd_param_bits=8;io.spi_mode=3;io.trans_queue_depth=1;io.user_ctx=&lcdCompletion;io.on_color_trans_done=[](esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*p)->bool{return static_cast<drivers::LcdDmaCompletion*>(p)->fromIsr();};
 e=esp_lcd_new_panel_io_spi(SPI3_HOST,&io,&lcdIo);
 esp_lcd_panel_dev_config_t cfg{};cfg.reset_gpio_num=pins::LCD_RST;cfg.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;cfg.bits_per_pixel=16;
 if(e==ESP_OK)e=esp_lcd_new_panel_st7789(lcdIo,&cfg,&lcdPanel);
 if(e==ESP_OK)e=esp_lcd_panel_reset(lcdPanel);if(e==ESP_OK)e=esp_lcd_panel_init(lcdPanel);
 if(e==ESP_OK)e=esp_lcd_panel_swap_xy(lcdPanel,false);if(e==ESP_OK)e=esp_lcd_panel_mirror(lcdPanel,false,false);
 if(e==ESP_OK)e=esp_lcd_panel_invert_color(lcdPanel,true);if(e==ESP_OK)e=esp_lcd_panel_disp_on_off(lcdPanel,true);
 if(e!=ESP_OK||cancel){cleanupLcd();return e!=ESP_OK?esp_err_to_name(e):"cancelled";}return nullptr;
},[](void*){JsonDocument s;lcdInitialization.read(s.to<JsonObject>());copy(lcdInitError,sizeof lcdInitError,s["error"]|"");lcdReady=!*lcdInitError;backlight.panelReady(lcdReady);backlight.poll(policies);});
static void startLcdInit(){JsonDocument r;if(auto e=lcdInitialization.start("initialize",r.to<JsonObject>()))copy(lcdInitError,sizeof lcdInitError,e);}
static constexpr DisplayComponent lcdComponent{"st7789","lcd","spi",boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT,DisplayEvidence::TransportComplete,"spi_dma","on_demand"};
#include "drivers/esp-lcd-spi/Presentation.inc"
#include "Power.inc"
static void beginNativeDevices(){
 rtc_gpio_hold_dis(gpio_num_t(pins::POWER_HOLD));pinMode(pins::POWER_HOLD,OUTPUT);digitalWrite(pins::POWER_HOLD,HIGH);
 (void)wishos_nimble_external_memory();if(auto error=backlight.initialize())event("/dev/backlight","failed",error);
 pinMode(pins::KEY_USER,INPUT_PULLUP);
 inputRaw=buttonMask();buttonEdges=xQueueCreate(64,sizeof(ButtonEdge));
 if(buttonEdges){attachInterrupt(pins::KEY_USER,captureButtonEdge,CHANGE);}else event("/dev/buttons","failed","input_queue_allocation_failed");
 pinMode(pins::CHARGE_SENSE,INPUT);startLcdInit();startPowerSample();
}
static void registerNativeDevices(){
 core.add({"/dev/backlight","read,describe,configure","display backlight",[](const char*m,JsonObjectConst a,JsonObject o){return backlight.invoke(m,a,o,policies,[](const char*text,size_t n){return preferences.putString("policy",text)==n;});},[](JsonObject o){backlight.describe(policies,o);},"read,describe"});
 registerAudio();
 registerPower();
 core.add({"/dev/ble","read,configure,start,restart,stop,write,scan,scan_results,scan_stop,connect,gatt_read,gatt_write,subscribe","Bluetooth GATT, HID and central",bleHandler,[](JsonObject o){for(auto m:{"start","restart","stop","connect","gatt_read","gatt_write","subscribe"})asyncContract(o,m,"/dev/ble",{"operation_completed","operation_failed"})["operation_id"]="operation_id";asyncContract(o,"scan","/dev/ble",{"scan_completed"})["operation_id"]="operation_id";o["modes"]="gatt,hid,central";},"read,scan_results"});
 core.add({"/sys/navigation","read,configure","protected system input",[](const char*m,JsonObjectConst a,JsonObject o){return navigation.invoke(m,a,o);},[](JsonObject o){navigation.describe(o);},"read"});
 core.add({"/sys/ui","read,capabilities,hit_test,elements,font_check,validate,fonts,font_register,font_unregister,create,activate,close,back,open,update,refresh,app_load,validate_app","runtime presentation",uiHandler,uiServiceDescription,"read,capabilities,hit_test,elements,font_check,fonts,validate,validate_app"});
 core.add({"/dev/display","read,drivers","LCD presentation component",lcdHandler,[](JsonObject o){lcdHandler("read",JsonObjectConst(),o);},"read,drivers"});
 core.add({"/dev/buttons","read,describe,configure","runtime button gestures",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure"))return buttonGestures.configure(a,o,[](const char*s,size_t n){return preferences.putString("buttons",s)==n;});if(!strcmp(m,"describe"))buttonGestures.describe(o);else if(!strcmp(m,"read"))buttonGestures.read(o);else return "unsupported_method";return nullptr;},[](JsonObject o){buttonGestures.describe(o);},"read,describe"});
 core.add({"/sys/config","read,describe,write,validate","device service policy",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"describe")){policies.describe(o["settings"].to<JsonArray>());return nullptr;}if(!strcmp(m,"read")){policies.read(a["scope"]|"system",o);return nullptr;}if(strcmp(m,"write")&&strcmp(m,"validate"))return "unsupported_method";return policies.apply(a,o,!strcmp(m,"validate"),[](const char*s,size_t n){return preferences.putString("policy",s)==n;});},nullptr,"read,describe,validate"});
 restoreBleConfiguration();
 JsonDocument startup;for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")){
  if(!deserializeJson(startup,asset.data,asset.size)){
   if(startup["navigation"].is<JsonObjectConst>())if(auto e=navigation.configure(startup["navigation"]))event("/sys/navigation","failed",e);
   if(startup["buttons"].is<JsonObjectConst>()){JsonDocument out;if(auto e=buttonGestures.configure(startup["buttons"],out.to<JsonObject>()))event("/dev/buttons","failed",e);}
  }break;
 }
 String buttons=preferences.getString("buttons","{}");buttonGestures.restore(buttons.c_str());
 const char*factory="{}";for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/defaults.json")){factory=asset.data;break;}
 policies.defaults(factory);String saved=preferences.getString("policy","{}");policies.restore(saved.c_str());
}
static void pollNativeDevices(){backlight.poll(policies);publishPresentation();microphone.poll();pollBle();lcdInitialization.poll();pollPower();pollUiPresentation();lcdScheduler.tick(uptime());pollButtonInput();}
