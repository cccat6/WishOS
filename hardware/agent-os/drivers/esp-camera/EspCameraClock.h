#pragma once
#include <stdint.h>
#include <initializer_list>
#include <sdkconfig.h>
#if CONFIG_IDF_TARGET_ESP32S3
#include <hal/cam_ll.h>
#include <esp_clk_tree.h>
#include <esp_private/esp_clk_tree_common.h>
#include <esp_private/periph_ctrl.h>
#endif
namespace notesos::drivers {
// Native S3 clock binding. Other camera platforms keep their SDK clock path.
class EspCameraClock {
 struct Divider{uint32_t n=0,a=0,b=0;};
 uint32_t requested_=0,sourceHz_=0,applied_=0;Divider divider_;bool leased_=false;
 static bool divide(uint32_t rate,uint32_t hz,bool integer,Divider&out){
  if(!hz||rate/hz<2||rate/hz>=256)return false;uint32_t rem=rate%hz;
  if(integer){if(rem)return false;out={rate/hz,0,0};return true;}
  if(!rem)return false;uint32_t x=rem,y=hz;while(y){uint32_t next=x%y;x=y;y=next;}
  uint32_t a=hz/x,b=rem/x;if(a>=64||b>=64)return false;out={rate/hz,a,b};return true;
 }
#if CONFIG_IDF_TARGET_ESP32S3
 cam_clock_source_t source_=CAM_CLK_SRC_PLL160M;
#endif
public:
 static constexpr bool exactSupported(){
#if CONFIG_IDF_TARGET_ESP32S3
  return true;
#else
  return false;
#endif
 }
 static const char*validate(uint32_t hz){
#if CONFIG_IDF_TARGET_ESP32S3
  Divider out;for(bool integer:{true,false})for(uint32_t rate:{160000000u,240000000u})if(divide(rate,hz,integer,out))return nullptr;
  return "camera_clock_not_representable";
#else
  (void)hz;return nullptr;
#endif
 }
 const char*apply(uint32_t hz){
  if(auto error=validate(hz))return error;
#if CONFIG_IDF_TARGET_ESP32S3
  const cam_clock_source_t sources[]={CAM_CLK_SRC_PLL160M,CAM_CLK_SRC_PLL240M};uint32_t rates[2]{};for(unsigned i=0;i<2;++i)esp_clk_tree_src_get_freq_hz(soc_module_clk_t(sources[i]),ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&rates[i]);
  Divider plan;bool found=false;for(bool integer:{true,false}){for(unsigned i=0;i<2;++i)if(divide(rates[i],hz,integer,plan)){source_=sources[i];found=true;break;}if(found)break;}
  if(!found)return "camera_clock_source_unavailable";
  if(esp_clk_tree_enable_src(soc_module_clk_t(source_),true)!=ESP_OK)return "camera_clock_enable_failed";leased_=true;
  // Initialization owns the camera and no frame has been exposed. Use the SDK
  // RCC guard and shadow-register update; startup settling discards transition frames.
  PERIPH_RCC_ATOMIC(){cam_ll_enable_clk(0,true);cam_ll_select_clk_src(0,source_);cam_ll_set_group_clock_coeff(0,plan.n,plan.a,plan.b);LCD_CAM.cam_ctrl.cam_update=1;}
  cam_clock_source_t actual;cam_ll_get_clk_src(&LCD_CAM,&actual);Divider got{LCD_CAM.cam_ctrl.cam_clkm_div_num,LCD_CAM.cam_ctrl.cam_clkm_div_a,LCD_CAM.cam_ctrl.cam_clkm_div_b};uint32_t actualRate=0;
  if(actual!=source_||got.n!=plan.n||got.a!=plan.a||got.b!=plan.b||esp_clk_tree_src_get_freq_hz(soc_module_clk_t(actual),ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&actualRate)!=ESP_OK)return "camera_clock_not_applied";
  uint32_t denominator=got.a?got.a:1;if(uint64_t(actualRate)*denominator!=uint64_t(hz)*(got.n*denominator+got.b))return "camera_clock_not_applied";
  requested_=hz;sourceHz_=actualRate;divider_=got;applied_=hz;return nullptr;
#else
  return nullptr;
#endif
 }
 void release(){
#if CONFIG_IDF_TARGET_ESP32S3
  if(leased_)esp_clk_tree_enable_src(soc_module_clk_t(source_),false);
#endif
  leased_=false;applied_=0;
 }
 template<class Object>void read(Object out)const{
#if CONFIG_IDF_TARGET_ESP32S3
  out["requested_hz"]=requested_;out["applied_hz"]=applied_;out["source_hz"]=sourceHz_;out["integer_divider"]=divider_.n;out["fraction_numerator"]=divider_.b;out["fraction_denominator"]=divider_.a;out["source_lease"]=leased_;
#else
  (void)out;
#endif
 }
};
}
