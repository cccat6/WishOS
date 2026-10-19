#pragma once
#include "gpio.h"
using ledc_mode_t=int;using ledc_timer_t=int;using ledc_channel_t=int;using ledc_clk_cfg_t=int;using ledc_timer_bit_t=int;
inline constexpr int LEDC_LOW_SPEED_MODE=0,LEDC_INTR_DISABLE=0,LEDC_USE_APB_CLK=1;
struct ledc_timer_config_t{int speed_mode,duty_resolution,timer_num;uint32_t freq_hz;int clk_cfg;bool deconfigure;};
struct ledc_channel_config_t{int gpio_num,speed_mode,channel,intr_type,timer_sel;uint32_t duty;int hpoint,sleep_mode;struct{unsigned output_invert:1;}flags;};
inline ledc_timer_config_t backlightTimers[4]{};inline ledc_channel_config_t backlightChannels[8]{};
inline int ledc_timer_config(const ledc_timer_config_t*p){++backlightCalls;if(backlightFailure==1)return -1;backlightTimers[p->timer_num]=*p;if(p->deconfigure)backlightTimers[p->timer_num].freq_hz=0;return ESP_OK;}
inline int ledc_channel_config(const ledc_channel_config_t*p){++backlightCalls;if(backlightFailure==2)return -1;backlightChannels[p->channel]=*p;return ESP_OK;}
inline int ledc_set_duty_and_update(int,int,uint32_t,int){++backlightCalls;return -1;} // Fade ISR is not installed.
inline uint32_t backlightPendingDuty[8]{};
inline int ledc_set_duty(int,int channel,uint32_t duty){++backlightCalls;if(backlightFailure==3)return -1;backlightPendingDuty[channel]=duty;return ESP_OK;}
inline int ledc_update_duty(int,int channel){++backlightCalls;if(backlightFailure==4)return -1;backlightChannels[channel].duty=backlightPendingDuty[channel];return ESP_OK;}

inline int ledc_timer_pause(int,int){++backlightCalls;return ESP_OK;}
inline int ledc_stop(int,int channel,uint32_t){++backlightCalls;backlightChannels[channel].duty=0;return ESP_OK;}
