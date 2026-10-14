#pragma once
#include "PowerSense.h"
#include <stddef.h>

namespace notesos {
// Timing policies are mutable; the ADC clamp circuit is a BSP capability.
struct PowerSenseConfig {
  uint32_t sampleMs=100, presenceDebounceMs=200, recoveryGuardMs=1000;
  uint32_t stableWindowMs=500, stableToleranceMv=20;
  bool operator==(const PowerSenseConfig&b)const {
    return sampleMs==b.sampleMs && presenceDebounceMs==b.presenceDebounceMs &&
      recoveryGuardMs==b.recoveryGuardMs && stableWindowMs==b.stableWindowMs &&
      stableToleranceMv==b.stableToleranceMv;
  }
  uint64_t staleMs()const {return sampleMs*3u>1000u?sampleMs*3u:1000u;}
};
struct PowerReading {
  uint64_t atMs=0,lastValidAtMs=0;
  uint32_t adcMv=0,batteryMv=0,lastValidMv=0;
  bool sampled=false,valid=false;
  int externalPower=-1;
  const char*reason="not_sampled";
};
struct PresenceChange {bool changed=false,initial=false;int value=-1;};
class PowerMonitor {
  const bool shared_;
  const float divider_;
  PowerSenseConfig cfg_;
  PowerReading state_;
  int candidate_=-1,confirmed_=-1;
  uint64_t candidateAt_=0,guardAt_=0,stableAt_=0;
  bool recovering_=true,stable_=false;
  uint32_t minimum_=0,maximum_=0;
  uint32_t samples_[5]{};unsigned count_=0,next_=0,stableCount_=0;
  void invalidate(uint64_t now,const char*reason){
    state_.valid=false;state_.batteryMv=0;state_.reason=reason;
    recovering_=true;guardAt_=now;stable_=false;stableCount_=0;count_=next_=0;
  }
  uint32_t median()const {
    uint32_t values[5];for(unsigned i=0;i<count_;++i)values[i]=samples_[i];
    for(unsigned i=1;i<count_;++i){auto v=values[i];unsigned j=i;while(j&&values[j-1]>v){values[j]=values[j-1];--j;}values[j]=v;}
    return values[count_/2];
  }
public:
  explicit PowerMonitor(bool shared,float divider=2.0f):shared_(shared),divider_(divider){}
  bool shared()const{return shared_;}
  const PowerSenseConfig&config()const{return cfg_;}
  void reset(){state_.sampled=false;state_.valid=false;state_.externalPower=-1;state_.reason="not_sampled";candidate_=-1;recovering_=true;stable_=false;count_=next_=stableCount_=0;}
  void configure(const PowerSenseConfig&cfg){if(!(cfg==cfg_)){cfg_=cfg;reset();}}
  PowerReading read(uint64_t now)const {
    auto s=state_;
    if(s.sampled&&now-s.atMs>cfg_.staleMs()){s.valid=false;s.batteryMv=0;s.externalPower=-1;s.reason="sample_stale";}
    return s;
  }
  PresenceChange update(uint64_t now,uint32_t adcMv,float gain=1.0f){
    PresenceChange change;
    if(!state_.sampled||now-state_.atMs>cfg_.staleMs()){
      invalidate(now,"settling");candidate_=-1;state_.externalPower=-1;
    }
    state_.sampled=true;state_.atMs=now;state_.adcMv=adcMv;
    // Hysteresis classifies the clamp, not battery charge/full state.
    if(shared_){
      int observed=adcMv<=100?1:adcMv>=200?0:-1;
      if(observed<0){candidate_=-1;state_.externalPower=-1;}
      else {
        if(observed!=candidate_){candidate_=observed;candidateAt_=now;}
        if(now-candidateAt_>=cfg_.presenceDebounceMs){
          if(confirmed_!=observed){change={true,confirmed_<0,observed};confirmed_=observed;}
          state_.externalPower=observed;
        }else state_.externalPower=-1;
      }
    }else state_.externalPower=-1;
    auto raw=decodePowerSample(adcMv,shared_,gain,divider_);
    if(!raw.batteryValid||(shared_&&state_.externalPower!=0)){
      invalidate(now,shared_&&adcMv<=100?"external_power_battery_unavailable":raw.batteryValid?"power_transition":raw.reason);
      return change;
    }
    samples_[next_]=adcMv;next_=(next_+1)%5;if(count_<5)++count_;
    if(recovering_){
      if(!stable_){stable_=true;stableAt_=now;minimum_=maximum_=adcMv;stableCount_=1;}
      else {
        if(adcMv<minimum_)minimum_=adcMv;if(adcMv>maximum_)maximum_=adcMv;
        if(maximum_-minimum_>cfg_.stableToleranceMv){stableAt_=now;minimum_=maximum_=adcMv;stableCount_=1;}
        else ++stableCount_;
      }
      if(now-guardAt_<cfg_.recoveryGuardMs || now-stableAt_<cfg_.stableWindowMs || stableCount_<3){
        state_.valid=false;state_.batteryMv=0;state_.reason="settling";return change;
      }
      recovering_=false;
    }
    state_.valid=true;state_.batteryMv=uint32_t(median()*divider_*gain+0.5f);
    state_.lastValidMv=state_.batteryMv;state_.lastValidAtMs=now;state_.reason="measured_filtered";
    return change;
  }
};

}
