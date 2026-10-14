#pragma once
#include "audio/Playback.h"
#include <driver/i2s_std.h>
#include <driver/gpio.h>
namespace notesos::audio {
// A standalone I2S amplifier adapter. Shared-clock codec boards provide a duplex
// adapter instead; this one does not commandeer an already-owned I2S controller.
class EspI2sOutput final:public OutputBackend {
 i2s_port_t port_;gpio_num_t bclk_,ws_,data_,amp_;bool ampHigh_;i2s_chan_handle_t tx_=nullptr;
 std::atomic<uint32_t>sent_{0};uint32_t drainAfter_=0;unsigned channels_=1;
 static bool sent(i2s_chan_handle_t,i2s_event_data_t*,void*p){static_cast<EspI2sOutput*>(p)->sent_.fetch_add(1);return false;}
public:
 EspI2sOutput(i2s_port_t port,gpio_num_t bclk,gpio_num_t ws,gpio_num_t data,gpio_num_t amp=GPIO_NUM_NC,bool ampHigh=true):port_(port),bclk_(bclk),ws_(ws),data_(data),amp_(amp),ampHigh_(ampHigh){}
 void describe(JsonObject o)const override{o["adapter"]="esp-idf-i2s-standard-tx";o["sample_rates_hz"]="8000,16000,24000,32000,44100,48000";o["channels"]="1,2";o["pcm_bits"]=16;o["duplex"]="exclusive_controller";o["completion_basis"]="dma_ring_drained";}
 const char*validate(PcmFormat f)const override{if(f.channels!=1&&f.channels!=2)return "audio_channels";switch(f.rate){case 8000:case 16000:case 24000:case 32000:case 44100:case 48000:return nullptr;default:return "audio_sample_rate";}}
 const char*open(PcmFormat f)override{
  if(auto e=validate(f))return e;i2s_chan_config_t cfg=I2S_CHANNEL_DEFAULT_CONFIG(port_,I2S_ROLE_MASTER);cfg.dma_desc_num=4;cfg.dma_frame_num=128;cfg.auto_clear_after_cb=true;
  if(i2s_new_channel(&cfg,&tx_,nullptr)!=ESP_OK)return "audio_output_controller_busy";
  i2s_std_config_t s{};s.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(f.rate);s.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,f.channels==1?I2S_SLOT_MODE_MONO:I2S_SLOT_MODE_STEREO);s.gpio_cfg.mclk=I2S_GPIO_UNUSED;s.gpio_cfg.bclk=bclk_;s.gpio_cfg.ws=ws_;s.gpio_cfg.dout=data_;s.gpio_cfg.din=I2S_GPIO_UNUSED;
  i2s_event_callbacks_t events{};events.on_sent=sent;sent_=0;drainAfter_=0;channels_=f.channels;
  if(i2s_channel_init_std_mode(tx_,&s)!=ESP_OK||i2s_channel_register_event_callback(tx_,&events,this)!=ESP_OK||i2s_channel_enable(tx_)!=ESP_OK){close();return "audio_output_init_failed";}
  if(amp_!=GPIO_NUM_NC){gpio_set_direction(amp_,GPIO_MODE_OUTPUT);gpio_set_level(amp_,ampHigh_);}return nullptr;
 }
 const char*write(const int16_t*p,size_t n,size_t&accepted)override{accepted=0;size_t bytes=0;auto e=i2s_channel_write(tx_,p,n*sizeof(int16_t),&bytes,0);accepted=bytes/2;if(bytes)drainAfter_=sent_.load()+5;return e==ESP_OK||e==ESP_ERR_TIMEOUT?nullptr:"audio_output_failed";}
 bool drained()override{return int32_t(sent_.load()-drainAfter_)>=0;}
 void close()override{if(amp_!=GPIO_NUM_NC)gpio_set_level(amp_,!ampHigh_);if(tx_){i2s_channel_disable(tx_);i2s_del_channel(tx_);tx_=nullptr;}}
};
}
