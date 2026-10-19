#pragma once
#include "Policy.h"
#include <initializer_list>
namespace notesos {
struct SdConfiguration {
  bool automatic=true,experimental=false,experimentalIo=false;
  uint32_t fixedHz=20000000,ceilingHz=80000000;
  int8_t partition=-1;
  bool sameLink(const SdConfiguration&o)const{return automatic==o.automatic&&fixedHz==o.fixedHz&&ceilingHz==o.ceilingHz&&experimental==o.experimental;}
  bool sameVolume(const SdConfiguration&o)const{return partition==o.partition&&experimentalIo==o.experimentalIo;}
  uint32_t cacheProfile(uint32_t hostHz,uint32_t protocolHz,bool hs)const{
    uint32_t h=2166136261u;
    // Version is part of the cache identity; old probe results are not authority.
    for(uint32_t v:{uint32_t(2),hostHz,ceilingHz,protocolHz,uint32_t(hs),uint32_t(experimental)})
      for(int i=0;i<4;++i){h^=uint8_t(v);h*=16777619u;v>>=8;}
    return h;
  }
};
inline SdConfiguration sdConfiguration(const Policies&p){
  JsonDocument d;p.resolve("/sys/bus","system",d.to<JsonObject>());SdConfiguration c;
  c.automatic=!strcmp(d["sd_spi_mode"]|"auto","auto");c.fixedHz=d["sd_spi_hz"]|20000000u;
  c.ceilingHz=d["sd_auto_ceiling_hz"]|80000000u;c.experimental=d["sd_allow_experimental"]|false;
  c.experimentalIo=d["sd_allow_experimental_io"]|false;const char*part=d["sd_partition"]|"auto";
  c.partition=!strcmp(part,"auto")?-1:!strcmp(part,"superfloppy")?0:int8_t(atoi(part));return c;
}
struct SdLinkState {
  uint32_t requestedHz=0,effectiveHz=0,cardDefaultMaxHz=0,cardModeMaxHz=0,candidateCount=0;
  uint64_t cardKey=0;SdConfiguration applied;
  bool ready=false,highSpeedSupported=false,highSpeedEnabled=false,cacheHit=false,readOnly=false,outOfSpec=false;
  int8_t partition=-1;
  char activeMode[8]="none",error[48]="not_probed",volumeError[40]="not_attempted";
};
}
