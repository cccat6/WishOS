static SdMmcVolume sdVolume(core,vfs,pins::SD_CLK,pins::SD_CMD,pins::SD_D0,pins::SD_MAX_HZ);
static SemaphoreHandle_t i2cMutex=xSemaphoreCreateMutex();
static drivers::WireI2c wireI2c(Wire,i2cMutex,pins::I2C_HZ);
static drivers::Pca9557 expander(wireI2c.bus);
static drivers::Qmi8658 imu(wireI2c.bus,drivers::Qmi8658::Profile::RevA);
static drivers::Ft6336 touch(wireI2c.bus,0x38,{240,320,true,false,true});
static std::atomic<uint32_t>inputPresentation{0};
struct InputTask{void(*run)(void*);void*context;};
static void inputTask(void*p){auto entry=*static_cast<InputTask*>(p);delete static_cast<InputTask*>(p);entry.run(entry.context);vTaskDelete(nullptr);}
static drivers::Ft6336 touchInputBackend(wireI2c.bus,0x38,{240,320,true,false,true});
static drivers::Ft6336Input touchInput(core,touchInputBackend,{[](void(*run)(void*),void*context){auto entry=new(std::nothrow)InputTask{run,context};if(!entry)return false;if(xTaskCreate(inputTask,"wish-touch",4096,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},6144},[](){return inputPresentation.load();},[](unsigned ms){vTaskDelay(pdMS_TO_TICKS(ms?ms:1));});
static drivers::Sampling imuSampling(core,imu,"/dev/imu"),touchSampling(core,touch,"/dev/touch");
static SystemNavigation navigation(core,applications,1,1,0);
static char appScope[48]="system";
static constexpr Setting SETTINGS[]={
 Setting{"/dev/audio","start_timeout_ms","integer","3000","",1,60000}.inSystem().at("next_start"),
 Setting{"/sys/ui","font_file","string","\"\"","",0,255}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","font_cache_glyphs","integer","128","",0,1024}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","image_cache_bytes","integer","131072","",0,1048576}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","font_face_cache","integer","8","",0,32}.inSystem().at("next_ui_prepare"),
 Setting{"/sys/ui","retain_renderer_on_switch","boolean","true"}.inSystem().at("next_application_exit"),
 {"/dev/display","pending","enum","\"latest\"","latest,fifo,reject"},
 {"/dev/display","prepare_timeout_ms","integer","30000","",1,4294967295.0},
 {"/dev/display","queue_limit","integer","2","",1,4},
 {"/dev/display","driver","enum","\"lcd.rgb565.v1\"","lcd.rgb565.v1"},
 {"/sys/ui","png_decode_budget_bytes","integer","262144","",1024,1048576}
};
static Policies policies(SETTINGS,sizeof SETTINGS/sizeof *SETTINGS);
static bool resolveFile(const char*path,char*full,const char*&relative,FileProvider*&provider){if(!normalizePath("/",path,full,256))return false;provider=vfs.resolve(full,relative);return provider!=nullptr;}
#include "UiService.h"
#include "LcdDisplay.inc"
#include "Audio.inc"
#include "Camera.inc"
#include "drivers/esp32-services/BleService.h"
static const char*defaultNfsExport(){return "/sd";}
static const char*defaultNfsProvider(){return "user";}
static const char*nativeStorage(const char*m,JsonObjectConst a,JsonObject o){return a["provider"]=="user"?"use_dev_sd":flashVolume.invoke(m,a,o);}
static void buttonEvent(void*,const char*name,JsonObjectConst data){core.emit("/dev/buttons",name,data,0,uptime());}
static ButtonGestures buttonGestures({"BOOT"},nullptr,buttonEvent);
// GPIO edges are captured independently of rendering. Gesture interpretation,
// configuration, navigation and callbacks remain on the system event loop.
struct ButtonEdge {uint32_t at,pressed,presentation;};
static QueueHandle_t buttonEdges=nullptr;static volatile uint32_t inputOverruns=0;static uint32_t inputRaw=0;
static void IRAM_ATTR captureButtonEdge(){ButtonEdge e{millis(),gpio_get_level(gpio_num_t(pins::KEY))==0?1u:0u,inputPresentation.load(std::memory_order_relaxed)};if(xQueueSendFromISR(buttonEdges,&e,nullptr)!=pdTRUE)++inputOverruns;}
static void pollButtonInput(){if(!buttonEdges)return;ButtonEdge e;while(xQueueReceive(buttonEdges,&e,0)==pdTRUE){
 // Advance the previous level to its captured edge, then apply the new level.
 // This preserves a short press/release pair captured during a long render.
 uint64_t now=uptime(),captured=now-uint32_t(uint32_t(now)-e.at);navigation.poll(inputRaw,captured);buttonGestures.sample(0,inputRaw,e.at,e.presentation);inputRaw=e.pressed;
 navigation.poll(inputRaw,captured);buttonGestures.sample(0,inputRaw,e.at,e.presentation);
 }if(inputOverruns){inputOverruns=0;inputRaw=digitalRead(pins::KEY)==LOW?1:0;event("/dev/buttons","failed","input_edge_overflow");}auto now=uptime();navigation.poll(inputRaw,now);buttonGestures.sample(0,inputRaw,now,inputPresentation.load());inputPresentation=presentationState.active();
}
static void beginNativeDevices(){(void)wishos_nimble_external_memory();vfs.add(sdVolume.provider());Wire.begin(pins::SDA,pins::SCL);Wire.setTimeOut(10);pinMode(pins::KEY,INPUT_PULLUP);inputRaw=digitalRead(pins::KEY)==LOW?1:0;buttonEdges=xQueueCreate(64,sizeof(ButtonEdge));if(buttonEdges)attachInterrupt(pins::KEY,captureButtonEdge,CHANGE);else event("/dev/buttons","failed","input_queue_allocation_failed");startLcdInit();}
static void registerNativeDevices(){
 registerAudio();
 core.add({"/dev/ble","read,configure,start,restart,stop,write,scan,scan_results,scan_stop,connect,gatt_read,gatt_write,subscribe","Bluetooth GATT, HID and central",bleHandler,[](JsonObject o){for(auto m:{"start","restart","stop","connect","gatt_read","gatt_write","subscribe"})asyncContract(o,m,"/dev/ble",{"operation_completed","operation_failed"})["operation_id"]="operation_id";asyncContract(o,"scan","/dev/ble",{"scan_completed"})["operation_id"]="operation_id";o["modes"]="gatt,hid,central";},"read,scan_results"});
 core.add({"/dev/camera","read,describe,configure,start,capture,read_chunk,copy_frame,release,stop,cancel","camera frames",[](const char*m,JsonObjectConst a,JsonObject o){return camera.invoke(m,a,o);},[](JsonObject o){camera.describe(o);},"read,describe,read_chunk"});
 core.add({"/sys/navigation","read,configure","protected system input",[](const char*m,JsonObjectConst a,JsonObject o){return navigation.invoke(m,a,o);},[](JsonObject o){navigation.describe(o);},"read"});
 core.add({"/dev/sd","read,describe,configure,probe,mount,unmount,space,cancel","SDMMC removable volume",[](const char*m,JsonObjectConst a,JsonObject o){return sdVolume.invoke(m,a,o);},[](JsonObject o){sdVolume.describe(o);},"read,describe"});
 core.add({"/dev/imu",drivers::Sampling::Methods,"six-axis inertial sensor",[](const char*m,JsonObjectConst a,JsonObject o){return imuSampling.invoke(m,a,o);},[](JsonObject o){imuSampling.describe(o);},"read,describe"});
 core.add({"/dev/touch","describe,read,configure,measure,cancel,configure_stream","two-contact touch sensor",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure_stream"))return touchInput.configure(a,o);auto e=touchSampling.invoke(m,a,o);if(!e&&!strcmp(m,"read"))touchInput.read(o);return e;},[](JsonObject o){touchSampling.describe(o);touchInput.describe(o);},"read,describe"});
 core.add({"/sys/ui","read,capabilities,hit_test,validate,fonts,font_register,font_unregister,create,activate,close,back,open,update,refresh,app_load,validate_app","runtime presentation",uiHandler,uiAsyncDescription,"read,capabilities,hit_test,fonts,validate,validate_app"});
 core.add({"/dev/display","read,drivers","LCD presentation component",lcdHandler,[](JsonObject o){lcdHandler("read",JsonObjectConst(),o);},"read,drivers"});
 core.add({"/dev/buttons","read,describe,configure","runtime button gestures",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure"))return buttonGestures.configure(a,o,[](const char*s,size_t n){return preferences.putString("buttons",s)==n;});if(!strcmp(m,"describe"))buttonGestures.describe(o);else if(!strcmp(m,"read"))buttonGestures.read(o);else return "unsupported_method";return nullptr;},nullptr,"read,describe"});
 core.add({"/sys/config","read,describe,write,validate","device service policy",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"describe")){policies.describe(o["settings"].to<JsonArray>());return nullptr;}if(!strcmp(m,"read")){policies.read(a["scope"]|"system",o);return nullptr;}if(strcmp(m,"write")&&strcmp(m,"validate"))return "unsupported_method";return policies.apply(a,o,!strcmp(m,"validate"),[](const char*s,size_t n){return preferences.putString("policy",s)==n;});},nullptr,"read,describe,validate"});
 restoreBleConfiguration();
 String buttons=preferences.getString("buttons","{}");buttonGestures.restore(buttons.c_str());
 JsonDocument nav;for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")&&!deserializeJson(nav,asset.data,asset.size)&&nav["navigation"].is<JsonObjectConst>()){navigation.configure(nav["navigation"]);break;}
 String saved=preferences.getString("policy","{}");policies.restore(saved.c_str());
}
static void pollNativeDevices(){inputPresentation.store(presentationState.active(),std::memory_order_relaxed);microphone.poll();pollAudio();pollBle();camera.poll();sdVolume.poll();lcdInitialization.poll();imuSampling.poll(uptime());touchSampling.poll(uptime());touchInput.poll();pollUiPresentation();lcdScheduler.tick(uptime());pollButtonInput();}
