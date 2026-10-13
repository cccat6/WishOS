#pragma once
#include "Policy.h"
#include <math.h>
namespace notesos {
struct MotionConfig {
 bool orientation=true,tap=false;uint32_t sampleMs=80,stableMs=400,rotationOffset=0,tapCount=2,tapWindowMs=420,tapThreshold1=256,tapThreshold2=768;float dominance=0.25f;
 static const char*merge(JsonObjectConst a,MotionConfig&c){
  for(auto f:a){auto k=f.key().c_str();if(!Policies::choice("orientation,sample_ms,stable_ms,rotation_offset,tap,tap_count,tap_window_ms,tap_threshold1_mg,tap_threshold2_mg,dominance",k))return "unknown_motion_setting";
   if(!strcmp(k,"orientation")||!strcmp(k,"tap")){if(!f.value().is<bool>())return "motion_type";if(!strcmp(k,"tap"))c.tap=f.value();else c.orientation=f.value();}
   else if(!strcmp(k,"dominance")){if(!f.value().is<float>()||!isfinite(f.value().as<float>()))return "motion_type";c.dominance=f.value();}
   else{if(!f.value().is<uint32_t>())return "motion_type";uint32_t n=f.value();if(!strcmp(k,"sample_ms"))c.sampleMs=n;else if(!strcmp(k,"stable_ms"))c.stableMs=n;else if(!strcmp(k,"rotation_offset"))c.rotationOffset=n;else if(!strcmp(k,"tap_count"))c.tapCount=n;else if(!strcmp(k,"tap_window_ms"))c.tapWindowMs=n;else if(!strcmp(k,"tap_threshold1_mg"))c.tapThreshold1=n;else c.tapThreshold2=n;}
  }
  if(c.sampleMs<20||c.sampleMs>2000||c.stableMs<20||c.stableMs>10000||c.rotationOffset>270||c.rotationOffset%90||c.dominance<0.05f||c.dominance>0.8f||c.tapCount<1||c.tapCount>15||c.tapWindowMs<100||c.tapWindowMs>4900||(c.tapWindowMs-100)%320)return "motion_range";
  if(thresholdIndex(c.tapThreshold1,false)<0||thresholdIndex(c.tapThreshold2,true)<0||c.tapThreshold2<c.tapThreshold1)return "tap_threshold_range";
  return nullptr;
 }
 static int thresholdIndex(uint32_t value,bool high){const uint32_t low[]={64,128,256,384,512,640,768,960},upper[]={128,256,512,768,1024,1280,1536,1792};for(int i=0;i<8;++i)if(value==(high?upper[i]:low[i]))return i;return -1;}
 void describe(JsonObject o)const{o["orientation"]=orientation;o["sample_ms"]=sampleMs;o["stable_ms"]=stableMs;o["rotation_offset"]=rotationOffset;o["dominance"]=dominance;o["tap"]=tap;o["tap_count"]=tapCount;o["tap_window_ms"]=tapWindowMs;o["tap_threshold1_mg"]=tapThreshold1;o["tap_threshold2_mg"]=tapThreshold2;}
};
class OrientationInput {
 int candidate_=-1,applied_=-1;uint64_t since_=0,last_=0;
public:
 int current()const{return applied_;}
 void reset(){candidate_=applied_=-1;since_=last_=0;}
 // Ratios are independent of sensor range. Flat/ambiguous/zero-g samples keep
 // the last orientation; no quantized direction event is manufactured from noise.
 int sample(int x,int y,int z,uint64_t now,const MotionConfig&c){
  if(!c.orientation)return -1;float ax=fabsf(float(x)),ay=fabsf(float(y)),az=fabsf(float(z)),total=sqrtf(ax*ax+ay*ay+az*az);
  int next=-1;if(total>0&&fmaxf(ax,ay)>az&&fabsf(ax-ay)>c.dominance*total)next=(ax>ay?(x>0?90:270):(y>0?0:180));
  if(next<0){candidate_=-1;last_=now;return -1;}next=(next+c.rotationOffset)%360;
  if(candidate_!=next||now-last_>c.sampleMs*3){candidate_=next;since_=now;}last_=now;
  if(next!=applied_&&now-since_>=c.stableMs){applied_=next;return next;}return -1;
 }
};
}
