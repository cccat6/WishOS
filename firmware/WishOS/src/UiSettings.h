#pragma once
#include "Policy.h"
namespace notesos {
constexpr Setting uiDrawBufferSetting(unsigned width,unsigned height,const char*bytes){return Setting{"/sys/ui","draw_buffer_bytes","integer",bytes,"",double((width>height?width:height)*2),4294967295.0}.inSystem().at("next_ui_prepare");}
}
