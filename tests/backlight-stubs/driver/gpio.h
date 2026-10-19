#pragma once
#include <stdint.h>
using esp_err_t=int;using gpio_num_t=int;inline constexpr int ESP_OK=0,GPIO_MODE_OUTPUT=1;
inline int backlightCalls=0,backlightFailure=0,backlightLevels[64]{},backlightDirections[64]{};
inline const char*esp_err_to_name(int){return "mock_ledc_failure";}
inline int gpio_set_level(int gpio,int level){++backlightCalls;backlightLevels[gpio]=level;return ESP_OK;}
inline int gpio_set_direction(int gpio,int direction){++backlightCalls;backlightDirections[gpio]=direction;return ESP_OK;}
