// Included inside the board adapter. Link probing only uses card registers and
// status commands; filesystem discovery is a separate, read-only operation.
struct SdReference {
  cid_t cid{};csd_t csd{};scr_t scr{};uint32_t ocr=0;
  bool hasScr=false,hasOcr=false,hasSwitchStatus=false;
  uint64_t key=0;
};
static SdReference sdReference;
struct SdClockCache{uint64_t card=0;uint32_t profile=0,requested=0;};static SdClockCache sdClockCache;
static uint8_t sdSwitchStatus[64];
static uint32_t sdCandidateRequests[256],sdCandidateActual[256];

static uint32_t sdEffectiveHz(uint32_t requested){
  return spiClockDivToFrequency(nullptr,spiFrequencyToClockDiv(nullptr,requested));
}
static uint64_t sdCardKey(const uint8_t*cid){
  uint64_t h=14695981039346656037ULL;
  for(size_t i=0;i<16;++i){h^=cid[i];h*=1099511628211ULL;}
  return h;
}
static uint32_t sdDefaultSpeedHz(const csd_t&csd){
  static constexpr uint32_t unit[]={100000,1000000,10000000,100000000};
  static constexpr uint8_t multiplier[]={0,10,12,13,15,20,25,30,35,40,45,50,55,60,70,80};
  uint8_t code=csd.csd[3],u=code&7,v=(code>>3)&15;
  if(u>=4||!multiplier[v])return 0;
  uint64_t hz=uint64_t(unit[u])*multiplier[v]/10;
  // CSD may describe a card capable of HS; after CMD0 it is in Default Speed.
  return uint32_t(hz>25000000u?25000000u:hz);
}
static void sdResetBus(){
  sdVolumeSnapshot(false);
  if(cardProbed)SD.end();
  SPI.end();spiStarted=SPI.begin(pins::SCLK,pins::MISO,pins::MOSI,-1);
  digitalWrite(pins::PANEL_CS,HIGH);digitalWrite(pins::SD_CS,HIGH);
  mounted=false;sdVolumeAttempted=false;
}
static bool sdStartCard(){
  sdResetBus();cardProbed=true;
  if(!spiStarted)return false;
  boardSdSpi.setSckSpeed(400000);
  return SD.cardBegin(SdSpiConfig(pins::SD_CS,SHARED_SPI|USER_SPI_BEGIN,1000000,&boardSdSpi));
}
static bool sdReadReference(){
  auto card=SD.card();if(!card)return false;
  if(!card->readCID(&sdReference.cid)||!card->readCSD(&sdReference.csd))return false;
  sdReference.key=sdCardKey(reinterpret_cast<const uint8_t*>(&sdReference.cid));
  sdReference.hasOcr=card->readOCR(&sdReference.ocr);
  sdReference.hasScr=card->readSCR(&sdReference.scr);
  sdReference.hasSwitchStatus=sdReference.hasScr&&sdReference.scr.sdSpecVer()>101&&
                              card->cardCMD6(0x00ffffff,sdSwitchStatus);
  sdLink.highSpeedSupported=sdReference.hasSwitchStatus&&(sdSwitchStatus[13]&0x02);
  sdLink.readOnly=sdReference.csd.permWriteProtect()||sdReference.csd.tempWriteProtect();
  sdCanWrite=!sdLink.readOnly;
  sdLink.cardDefaultMaxHz=sdDefaultSpeedHz(sdReference.csd);
  sdLink.cardModeMaxHz=sdLink.cardDefaultMaxHz;
  sdLink.cardKey=sdReference.key;
  return sdLink.cardDefaultMaxHz!=0;
}
static bool sdSwitchHighSpeed(){
  if(!sdLink.highSpeedSupported)return false;
  if(!SD.card()->cardCMD6(0x80fffff1,sdSwitchStatus)||
     (sdSwitchStatus[16]&0x0f)!=1)return false;
  sdLink.highSpeedEnabled=true;sdLink.cardModeMaxHz=50000000;
  // Registers are compared in their selected mode; TRAN_SPEED may change.
  if(!SD.card()->readCSD(&sdReference.csd))return false;
  return true;
}
static bool sdVerifyRegisters(uint32_t requested){
  boardSdSpi.setSckSpeed(requested);
  auto card=SD.card();if(!card)return false;
  for(unsigned round=0;round<12;++round){
    cid_t cid{};csd_t csd{};
    if(sdCancellationRequested())return false;
    if(!card->readCID(&cid)||memcmp(&cid,&sdReference.cid,sizeof(cid))||
       !card->readCSD(&csd)||memcmp(&csd,&sdReference.csd,sizeof(csd)))return false;
    if(sdReference.hasOcr){uint32_t ocr=0;if(!card->readOCR(&ocr)||ocr!=sdReference.ocr)return false;}
    if(sdReference.hasScr){scr_t scr{};if(!card->readSCR(&scr)||memcmp(&scr,&sdReference.scr,sizeof(scr)))return false;}
    if(sdReference.hasSwitchStatus){
      if(!card->cardCMD6(0x00ffffff,sdSwitchStatus))return false;
      if(bool(sdSwitchStatus[13]&0x02)!=sdLink.highSpeedSupported||
         (sdSwitchStatus[16]&0x0f)!=(sdLink.highSpeedEnabled?1:0))return false;
    }
  }
  return true;
}
static bool sdTryClock(uint32_t requested,bool cached){
  if(!sdVerifyRegisters(requested)){
    boardSdSpi.setSckSpeed(1000000);
    return false;
  }
  sdLink.requestedHz=requested;sdLink.effectiveHz=sdEffectiveHz(requested);
  sdLink.ready=true;sdLink.cacheHit=cached;
  sdLink.outOfSpec=sdLink.effectiveHz>sdLink.cardModeMaxHz;
  copy(sdLink.error,sizeof(sdLink.error),"none");
  sdSetWritable(!sdLink.readOnly);
  return true;
}
static bool sdMountConfigured(const SdConfiguration&desiredConfig){
  sdTuned=true;sdCanWrite=true;mounted=false;sdVolumeAttempted=false;
  const char*mode=desiredConfig.automatic?"auto":"fixed";
  uint32_t fixed=desiredConfig.fixedHz,ceiling=desiredConfig.ceilingHz;
  bool experimental=desiredConfig.experimental;
  sdLink={};copy(sdLink.activeMode,sizeof(sdLink.activeMode),mode);
  sdLink.applied=desiredConfig;
  copy(sdLink.error,sizeof(sdLink.error),"card_init_failed");
  if(!strcmp(mode,"fixed")&&!sdClockExact(fixed,sdEffectiveHz)){
    copy(sdLink.error,sizeof(sdLink.error),"unsupported_exact_spi_clock");
    sdSetWritable(false);event("/dev/sd","link_failed",sdLink.error);return false;
  }
  if(!sdStartCard()||!sdReadReference()){
    copy(sdLink.error,sizeof(sdLink.error),"card_register_probe_failed");
    sdSetWritable(false);event("/dev/sd","link_failed",sdLink.error);return false;
  }
  uint32_t hostMax=getApbFrequency();
  uint32_t desired=!strcmp(mode,"fixed")?fixed:ceiling;
  if(desired>sdLink.cardDefaultMaxHz&&sdLink.highSpeedSupported){
    if(!sdSwitchHighSpeed()){
      sdLink.highSpeedEnabled=false;
      // CMD0 returns the card to default mode if switching was partial.
      if(!sdStartCard()||!sdReadReference()){
        copy(sdLink.error,sizeof(sdLink.error),"high_speed_recovery_failed");
        sdSetWritable(false);event("/dev/sd","link_failed",sdLink.error);return false;
      }
    }
  }
  uint32_t allowed=experimental?hostMax:sdLink.cardModeMaxHz;
  if(allowed>hostMax)allowed=hostMax;
  if(!strcmp(mode,"fixed")){
    sdLink.candidateCount=1;
    if(fixed>allowed)copy(sdLink.error,sizeof(sdLink.error),"card_mode_clock_exceeded");
    else if(sdTryClock(fixed,false)){event("/dev/sd","link_ready");return true;}
    else copy(sdLink.error,sizeof(sdLink.error),"fixed_clock_handshake_failed");
  }else{
    if(allowed>ceiling)allowed=ceiling;
    uint32_t cached=sdClockCache.requested;
    uint32_t profile=sdLink.applied.cacheProfile(hostMax,sdLink.cardModeMaxHz,sdLink.highSpeedEnabled);
    if(sdClockCache.card==sdReference.key&&
       sdClockCache.profile==profile&&cached&&
       sdEffectiveHz(cached)<=allowed){
      if(sdCancellationRequested()){copy(sdLink.error,sizeof sdLink.error,"cancelled");return false;}
      ++sdLink.candidateCount;
      if(sdTryClock(cached,true)){event("/dev/sd","link_ready");return true;}
    }
    size_t n=sdClockCandidates(hostMax,allowed,sdEffectiveHz,
                               sdCandidateRequests,sdCandidateActual,
                               sizeof(sdCandidateRequests)/sizeof(sdCandidateRequests[0]));
    for(size_t i=0;i<n;++i){
      if(sdCancellationRequested()){copy(sdLink.error,sizeof sdLink.error,"cancelled");return false;}
      ++sdLink.candidateCount;
      if(!sdTryClock(sdCandidateRequests[i],false))continue;
      sdClockCache={sdReference.key,profile,sdCandidateRequests[i]};
      event("/dev/sd","link_ready");return true;
    }
    copy(sdLink.error,sizeof(sdLink.error),n?"no_register_verified_clock":"no_clock_candidate");
  }
  boardSdSpi.setSckSpeed(1000000);sdSetWritable(false);
  event("/dev/sd","link_failed",sdLink.error);return false;
}
static bool sdMountVolume(const SdConfiguration&requestedConfig,bool retry=false){
  if(!sdLink.ready)return false;
  if(sdVolumeAttempted&&!retry)return mounted;
  sdVolumeAttempted=true;mounted=false;sdLink.partition=-1;
  sdVolumeSnapshot(false);

  sdLink.applied.partition=requestedConfig.partition;
  sdLink.applied.experimentalIo=requestedConfig.experimentalIo;
  if(sdLink.outOfSpec&&!requestedConfig.experimentalIo){
    copy(sdLink.volumeError,sizeof(sdLink.volumeError),"experimental_link_only");
    event("/dev/sd","volume_unavailable",sdLink.volumeError);return false;
  }
  uint8_t parts[5]={1,2,3,4,0};size_t n=5;
  if(requestedConfig.partition>=0){parts[0]=requestedConfig.partition;n=1;}
  for(size_t i=0;i<n;++i){
    if(SD.vol()->begin(SD.card(),true,parts[i])){
      mounted=true;sdLink.partition=parts[i];break;
    }
  }
  copy(sdLink.volumeError,sizeof(sdLink.volumeError),mounted?"none":"unsupported_or_unformatted");
  if(mounted)sdVolumeSnapshot(true);
  event("/dev/sd",mounted?"mounted":"volume_unavailable",sdLink.volumeError);
  return mounted;
}
