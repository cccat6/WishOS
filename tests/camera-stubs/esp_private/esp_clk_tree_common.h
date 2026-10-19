#pragma once
#include "hal/cam_ll.h"
inline int esp_clk_tree_enable_src(soc_module_clk_t,bool on){if(cameraClockEnableFailure&&on)return 2;if(on)++cameraClockLeases;else --cameraClockLeases;return 0;}
