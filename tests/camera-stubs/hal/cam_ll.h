#pragma once
#include <cstdint>
enum cam_clock_source_t{CAM_CLK_SRC_PLL160M=3,CAM_CLK_SRC_PLL240M=2};using soc_module_clk_t=cam_clock_source_t;
struct lcd_cam_dev_t{struct{uint32_t cam_clkm_div_num=0,cam_clkm_div_a=0,cam_clkm_div_b=0,cam_update=0;}cam_ctrl;};
inline lcd_cam_dev_t LCD_CAM;inline cam_clock_source_t cameraClockSource=CAM_CLK_SRC_PLL160M;
inline bool cameraClockEnableFailure=false,cameraClockReadbackFailure=false;inline unsigned cameraClockLeases=0;
inline void cam_ll_enable_clk(int,bool){}
inline void cam_ll_select_clk_src(int,cam_clock_source_t src){cameraClockSource=src;}
inline void cam_ll_set_group_clock_coeff(int,unsigned n,unsigned a,unsigned b){LCD_CAM.cam_ctrl.cam_clkm_div_num=n;LCD_CAM.cam_ctrl.cam_clkm_div_a=a;LCD_CAM.cam_ctrl.cam_clkm_div_b=b;}
inline void cam_ll_get_clk_src(lcd_cam_dev_t*,cam_clock_source_t*out){*out=cameraClockReadbackFailure?CAM_CLK_SRC_PLL160M:cameraClockSource;}
