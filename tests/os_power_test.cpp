#include "../hardware/agent-os/drivers/adc-power/PowerMonitor.h"
#include "fixtures/composition/Defaults.h"
#include <cassert>
#include <cmath>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>
using namespace notesos;
static void monitorTests(){
  PowerMonitor shared(true);
  for(uint64_t t=0;t<=2000;t+=100){auto e=shared.update(t,1950);assert(!shared.read(t).valid||shared.read(t).batteryMv>=3550);if(t==200)assert(e.changed&&e.initial&&e.value==0);}
  assert(shared.read(2000).valid&&shared.read(2000).batteryMv==3900);
  // First clamp sample immediately invalidates voltage; presence is debounced.
  assert(!shared.update(2100,0).changed);assert(!shared.read(2100).valid);assert(shared.read(2100).externalPower<0);
  assert(!shared.update(2200,0).changed);auto inserted=shared.update(2300,0);assert(inserted.changed&&!inserted.initial&&inserted.value==1);
  // Unplug recovery traverses values which would otherwise falsely look low.
  uint32_t recovery[]={300,800,1250,1550,1700,1810,1880,1930,1950,1950,1950,1950,1950,1950};
  uint64_t now=2400;bool removed=false;
  for(auto adc:recovery){auto e=shared.update(now,adc);removed|=e.changed&&e.value==0;auto s=shared.read(now);assert(!s.valid||s.batteryMv>=3550);if(now<3500)assert(!s.valid);now+=100;}
  assert(removed&&shared.read(now-100).valid&&shared.read(now-100).batteryMv==3900);
  // A single noisy sample is rejected by the median without inventing a low event.
  shared.update(now,1300);assert(shared.read(now).batteryMv==3900);now+=100;
  auto stale=shared.read(now+2000);assert(!stale.valid&&stale.externalPower<0);
  shared.update(now+2000,1900);assert(!shared.read(now+2000).valid); // no stability inferred over missing samples
  // Existing manufactured board cannot infer insertion from a low/broken ADC.
  PowerMonitor old(false);for(unsigned t=0;t<2000;t+=100){assert(!old.update(t,0).changed);assert(old.read(t).externalPower<0&&!old.read(t).valid);}
  for(unsigned t=2000;t<=3500;t+=100)old.update(t,1600);assert(old.read(3500).valid&&old.read(3500).batteryMv==3200);
  // Long slow ramps remain unsettled even after the minimum time passes.
  PowerMonitor ramp(true);for(unsigned t=0;t<1000;t+=100)ramp.update(t,0);
  for(unsigned i=0;i<80;++i){unsigned t=1000+i*100;ramp.update(t,1050+i*10);assert(!ramp.read(t).valid);}
  // Presence bounce must not emit an insertion/removal edge.
  PowerMonitor bounce(true);for(unsigned t=0;t<2000;t+=100)bounce.update(t,1900);
  assert(!bounce.update(2000,0).changed);assert(!bounce.update(2100,1900).changed);
  assert(!bounce.update(2200,0).changed);assert(!bounce.update(2300,1900).changed);
}
int main(){monitorTests();std::cout<<"Driver ADC conditioning, recovery/ramp/bounce/staleness and legacy profile passed\n";}
