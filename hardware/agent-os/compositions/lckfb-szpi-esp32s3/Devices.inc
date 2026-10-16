static SdMmcVolume sdVolume(core,vfs,pins::SD_CLK,pins::SD_CMD,pins::SD_D0,pins::SD_MAX_HZ);
static SemaphoreHandle_t i2cMutex=xSemaphoreCreateMutex();
static drivers::WireI2c wireI2c(Wire,i2cMutex,pins::I2C_HZ);
static drivers::Pca9557 expander(wireI2c.bus);
static drivers::Qmi8658 imu(wireI2c.bus,drivers::Qmi8658::Profile::RevA);
static drivers::Ft6336 touch(wireI2c.bus);
static drivers::Sampling imuSampling(core,imu,"/dev/imu"),touchSampling(core,touch,"/dev/touch");
static SystemNavigation navigation(core,applications,1,1,0);
static char appScope[48]="system";
static constexpr Setting SETTINGS[]={
 {"/dev/display","pending","enum","\"latest\"","latest,fifo,reject"},
 {"/dev/display","queue_limit","integer","2","",1,4},
 {"/dev/display","driver","enum","\"lcd.rgb565.v1\"","lcd.rgb565.v1"},
 {"/sys/ui","png_decode_budget_bytes","integer","262144","",1024,1048576}
};
static Policies policies(SETTINGS,sizeof SETTINGS/sizeof *SETTINGS);
static bool resolveFile(const char*path,char*full,const char*&relative,FileProvider*&provider){if(!normalizePath("/",path,full,256))return false;provider=vfs.resolve(full,relative);return provider!=nullptr;}
#include "UiService.h"
#include "LcdDisplay.inc"
static const char*defaultNfsExport(){return "/sd";}
static const char*defaultNfsProvider(){return "user";}
static const char*nativeStorage(const char*m,JsonObjectConst a,JsonObject o){return a["provider"]=="user"?"use_dev_sd":flashVolume.invoke(m,a,o);}
static void buttonEvent(void*,const char*name,JsonObjectConst data){core.emit("/dev/buttons",name,data,0,uptime());}
static ButtonGestures buttonGestures({"BOOT"},nullptr,buttonEvent);
static void beginNativeDevices(){vfs.add(sdVolume.provider());Wire.begin(pins::SDA,pins::SCL);Wire.setTimeOut(10);pinMode(pins::KEY,INPUT_PULLUP);startLcdInit();}
static void registerNativeDevices(){
 core.add({"/sys/navigation","read,configure","protected system input",[](const char*m,JsonObjectConst a,JsonObject o){return navigation.invoke(m,a,o);},[](JsonObject o){navigation.describe(o);},"read"});
 core.add({"/dev/sd","read,describe,configure,probe,mount,unmount,space,cancel","SDMMC removable volume",[](const char*m,JsonObjectConst a,JsonObject o){return sdVolume.invoke(m,a,o);},[](JsonObject o){sdVolume.describe(o);},"read,describe"});
 core.add({"/dev/imu",drivers::Sampling::Methods,"six-axis inertial sensor",[](const char*m,JsonObjectConst a,JsonObject o){return imuSampling.invoke(m,a,o);},[](JsonObject o){imuSampling.describe(o);},"read,describe"});
 core.add({"/dev/touch",drivers::Sampling::Methods,"two-contact touch sensor",[](const char*m,JsonObjectConst a,JsonObject o){return touchSampling.invoke(m,a,o);},[](JsonObject o){touchSampling.describe(o);},"read,describe"});
 core.add({"/sys/ui","read,capabilities,validate,fonts,font_register,font_unregister,create,activate,close,back,open,update,refresh,app_load,validate_app","runtime presentation",uiHandler,uiAsyncDescription,"read,capabilities,fonts,validate,validate_app"});
 core.add({"/dev/display","read,drivers","LCD presentation component",lcdHandler,[](JsonObject o){lcdHandler("read",JsonObjectConst(),o);},"read,drivers"});
 core.add({"/dev/buttons","read,describe,configure","runtime button gestures",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure"))return buttonGestures.configure(a,o,[](const char*s,size_t n){return preferences.putString("buttons",s)==n;});if(!strcmp(m,"describe"))buttonGestures.describe(o);else if(!strcmp(m,"read"))buttonGestures.read(o);else return "unsupported_method";return nullptr;},nullptr,"read,describe"});
 core.add({"/sys/config","read,describe,write,validate","device service policy",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"describe")){policies.describe(o["settings"].to<JsonArray>());return nullptr;}if(!strcmp(m,"read")){policies.read(a["scope"]|"system",o);return nullptr;}if(strcmp(m,"write")&&strcmp(m,"validate"))return "unsupported_method";return policies.apply(a,o,!strcmp(m,"validate"),[](const char*s,size_t n){return preferences.putString("policy",s)==n;});},nullptr,"read,describe,validate"});
 String buttons=preferences.getString("buttons","{}");buttonGestures.restore(buttons.c_str());
 JsonDocument nav;for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")&&!deserializeJson(nav,asset.data,asset.size)&&nav["navigation"].is<JsonObjectConst>()){navigation.configure(nav["navigation"]);break;}
 String saved=preferences.getString("policy","{}");policies.restore(saved.c_str());
}
static void pollNativeDevices(){sdVolume.poll();lcdInitialization.poll();imuSampling.poll(uptime());touchSampling.poll(uptime());pollUiPresentation();lcdScheduler.tick(uptime());uint32_t pressed=digitalRead(pins::KEY)==LOW?1:0;navigation.poll(pressed,uptime());buttonGestures.sample(0,pressed,uptime(),presentationState.active());}
