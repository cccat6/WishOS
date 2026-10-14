#include "compositions/agentic-notes-e6/Defaults.h"
#include "SdClockPlan.h"
#include "SdConfiguration.h"
#include <cassert>
#include <cstring>

namespace notesos {
static bool cardProtected=false,cancelProbe=false;
static bool sdCancellationRequested(){return cancelProbe;}
struct cid_t {uint8_t bytes[16];};
struct csd_t {
  uint8_t csd[16]{};
  bool permWriteProtect()const{return cardProtected;}
  bool tempWriteProtect()const{return false;}
  uint32_t capacity()const{return 8192;}
};
struct scr_t {uint8_t scr[8]{};int sdSpecVer()const{return 200;}};
struct SdSpiConfig {int cs,opt;uint32_t hz;void*driver;SdSpiConfig(int c,int o,uint32_t h,void*d):cs(c),opt(o),hz(h),driver(d){}};
constexpr int SHARED_SPI=0,USER_SPI_BEGIN=2,HIGH=1;
namespace pins {constexpr int SCLK=18,MISO=15,MOSI=17,PANEL_CS=19,SD_CS=16;}
struct FakeSPI {void end(){}bool begin(int,int,int,int){return true;}} SPI;
struct FakeSdSpi {uint32_t hz=1000000;void setSckSpeed(uint32_t h){hz=h;}} boardSdSpi;
static bool cardPresent=true,cardHighSpeed=true,cardModeHigh=false,cardFormatted=false,hsSwitchFails=false,cmd6Supported=true,csdChangesAfterSwitch=false;
static bool cardProbed=false,mounted=false,sdTuned=false,spiStarted=true,sdCanWrite=true,sdVolumeAttempted=false;
static bool providerWritable=true;
static uint32_t physicalMaxHz=80000000;
static unsigned contentReads=0;
void sdSetWritable(bool enabled){providerWritable=enabled;}
void sdVolumeSnapshot(bool){}
uint32_t spiFrequencyToClockDiv(void*,uint32_t hz){return hz==25000000?20000000:hz;}
uint32_t spiClockDivToFrequency(void*,uint32_t div){return div;}
uint32_t getApbFrequency(){return 80000000;}
void digitalWrite(int,int){}
void event(const char*,const char*,const char* =nullptr){}
struct FakeCard {
  bool valid()const{return cardPresent&&boardSdSpi.hz<=physicalMaxHz;}
  bool readCID(cid_t*c){if(!valid())return false;for(int i=0;i<16;++i)c->bytes[i]=uint8_t(i+3);return true;}
  bool readCSD(csd_t*c){if(!valid())return false;*c={};c->csd[3]=csdChangesAfterSwitch&&cardModeHigh?0x5a:0x32;return true;}
  bool readOCR(uint32_t*o){if(!valid())return false;*o=0x80ff8000;return true;}
  bool readSCR(scr_t*s){if(!valid())return false;*s={};s->scr[0]=2;return true;}
  bool cardCMD6(uint32_t arg,uint8_t*status){if(!valid()||!cmd6Supported)return false;
    if((arg&0x80000000u)&&hsSwitchFails)return false;
    if(arg&0x80000000u)cardModeHigh=true;
    memset(status,0,64);status[13]=cardHighSpeed?2:0;status[16]=cardModeHigh?1:0;return true;}
} card;
struct FakeVolume {bool begin(FakeCard*,bool,uint8_t part){++contentReads;return cardFormatted&&part==2;}} volume;
struct FakeSD {
  void end(){mounted=false;cardModeHigh=false;}
  bool cardBegin(SdSpiConfig){boardSdSpi.hz=1000000;cardModeHigh=false;return cardPresent;}
  FakeCard*card(){return &notesos::card;}
  FakeVolume*vol(){return &volume;}
} SD;
static SdLinkState sdLink;
Policies policies(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));
#include "drivers/esp32-services/SdLinkService.h"
static void setBus(const char*mode,uint32_t hz,bool experimental=false,bool experimentalIo=false){
  JsonDocument input,output;auto a=input.to<JsonObject>();a["scope"]="system";
  auto change=a["changes"].to<JsonArray>().add<JsonObject>();change["resource"]="/sys/bus";
  auto s=change["settings"].to<JsonObject>();s["sd_spi_mode"]=mode;s["sd_spi_hz"]=hz;
  s["sd_auto_ceiling_hz"]=80000000u;s["sd_allow_experimental"]=experimental;
  s["sd_allow_experimental_io"]=experimentalIo;
  assert(!policies.apply(input.as<JsonObjectConst>(),output.to<JsonObject>()));
}
}
int main(){using namespace notesos;
  csd_t csd{};csd.csd[3]=0x32;assert(sdDefaultSpeedHz(csd)==25000000);
  csd.csd[3]=0x5a;assert(sdDefaultSpeedHz(csd)==25000000);
  assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.ready&&sdLink.highSpeedEnabled);
  assert(sdLink.effectiveHz==40000000&&!sdLink.outOfSpec);
  assert(contentReads==0&&!mounted);
  assert(!sdMountVolume(sdConfiguration(policies))&&sdLink.ready&&contentReads>0);
  cardFormatted=true;assert(sdMountVolume(sdConfiguration(policies),true)&&mounted&&sdLink.partition==2);
  cardProtected=true;contentReads=0;
  assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.readOnly&&!providerWritable&&contentReads==0);
  cardProtected=false;
  contentReads=0;assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.cacheHit&&contentReads==0);
  hsSwitchFails=true;
  assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.ready&&!sdLink.highSpeedEnabled&&sdLink.effectiveHz==20000000);
  hsSwitchFails=false;
  setBus("fixed",80000000);
  assert(!sdMountConfigured(sdConfiguration(policies))&&!sdLink.ready);
  assert(!strcmp(sdLink.error,"card_mode_clock_exceeded"));
  setBus("fixed",80000000,true);
  assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.outOfSpec&&sdLink.effectiveHz==80000000);
  assert(contentReads==0);
  assert(!sdMountVolume(sdConfiguration(policies))&&!mounted&&contentReads==0);
  assert(!strcmp(sdLink.volumeError,"experimental_link_only"));
  setBus("fixed",80000000,true,true);
  assert(sdMountVolume(sdConfiguration(policies),true)&&mounted&&contentReads>0);
  setBus("auto",20000000);physicalMaxHz=16000000;
  assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.effectiveHz==16000000);
  physicalMaxHz=80000000;cardHighSpeed=false;cmd6Supported=true;
  assert(sdMountConfigured(sdConfiguration(policies))&&!sdLink.highSpeedEnabled&&sdLink.effectiveHz==20000000);
  assert(!sdLink.cacheHit); // HS-mode cache cannot carry over into standard mode.
  cardHighSpeed=true;csdChangesAfterSwitch=true;
  assert(sdMountConfigured(sdConfiguration(policies))&&sdLink.highSpeedEnabled&&sdLink.effectiveHz==40000000);
  assert(!sdLink.cacheHit);
  auto normal=sdConfiguration(policies);auto experimental=normal;experimental.experimental=true;
  assert(normal.cacheProfile(80000000,50000000,true)!=experimental.cacheProfile(80000000,50000000,true));
  assert(normal.cacheProfile(80000000,50000000,true)!=normal.cacheProfile(160000000,50000000,true));
  cancelProbe=true;contentReads=0;assert(!sdMountConfigured(sdConfiguration(policies))&&!sdLink.ready&&contentReads==0);cancelProbe=false;
  cardPresent=false;contentReads=0;
  assert(!sdMountConfigured(sdConfiguration(policies))&&!sdLink.ready&&contentReads==0);
}
