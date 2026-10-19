#pragma once
#include "hal/cam_ll.h"
constexpr int ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED=0;
inline int esp_clk_tree_src_get_freq_hz(soc_module_clk_t src,int,uint32_t*out){*out=src==CAM_CLK_SRC_PLL240M?240000000:160000000;return 0;}
