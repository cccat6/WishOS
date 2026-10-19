#pragma once
#include <ArduinoJson.h>
#include <driver/i2s_std.h>
#include <algorithm>
#include <stdint.h>
namespace notesos::audio {
// Standalone standard-I2S microphone transport. A shared-clock device selects
// its duplex adapter instead; controller assignment belongs to its composition.
class EspI2sInput {
public:
 struct Wiring {i2s_port_t port;gpio_num_t bclk,ws,data;unsigned sampleShift=16;i2s_std_slot_mask_t slot=I2S_STD_SLOT_LEFT;};
 static constexpr unsigned BlockFrames=160;
private:
 Wiring pins_;i2s_chan_handle_t rx_=nullptr;unsigned rate_=0;
public:
 explicit EspI2sInput(Wiring pins):pins_(pins){}
 static bool supports(unsigned rate){switch(rate){case 8000:case 16000:case 24000:case 32000:case 44100:case 48000:return true;default:return false;}}
 void describe(JsonObject o)const{o["adapter"]="esp-idf-i2s-standard-rx";o["input_roles"]="M";o["channels"]=1;o["pcm_bits"]=16;o["wire_bits"]=32;auto rates=o["sample_rates_hz"].to<JsonArray>();for(auto rate:{8000,16000,24000,32000,44100,48000})rates.add(rate);}
 const char*initialize(unsigned rate){
  if(!supports(rate))return "audio_sample_rate";if(pins_.sampleShift>31)return "audio_sample_shift";if(rx_)return "audio_input_already_open";
  i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(pins_.port,I2S_ROLE_MASTER);channel.dma_desc_num=4;channel.dma_frame_num=BlockFrames;
  auto error=i2s_new_channel(&channel,nullptr,&rx_);if(error!=ESP_OK)return esp_err_to_name(error);
  i2s_std_config_t config{};config.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate);config.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_MONO);config.slot_cfg.slot_mask=pins_.slot;
  config.gpio_cfg.mclk=I2S_GPIO_UNUSED;config.gpio_cfg.bclk=pins_.bclk;config.gpio_cfg.ws=pins_.ws;config.gpio_cfg.din=pins_.data;config.gpio_cfg.dout=I2S_GPIO_UNUSED;
  if((error=i2s_channel_init_std_mode(rx_,&config))!=ESP_OK||(error=i2s_channel_enable(rx_))!=ESP_OK){shutdown();return esp_err_to_name(error);}rate_=rate;return nullptr;
 }
 const char*receive(int16_t*out,size_t capacity,size_t&frames){
  frames=0;if(!rx_||!rate_)return "audio_input_not_started";if(!capacity)return nullptr;
  int32_t raw[BlockFrames];size_t bytes=0;auto error=i2s_channel_read(rx_,raw,std::min<size_t>(capacity,BlockFrames)*sizeof(int32_t),&bytes,10);
  if(error!=ESP_OK&&error!=ESP_ERR_TIMEOUT)return esp_err_to_name(error);if(bytes%sizeof(int32_t))return "audio_input_alignment";
  frames=bytes/sizeof(int32_t);for(size_t i=0;i<frames;++i){auto sample=raw[i]>>pins_.sampleShift;out[i]=int16_t(std::max<int32_t>(INT16_MIN,std::min<int32_t>(INT16_MAX,sample)));}return nullptr;
 }
 const char*shutdown(){if(!rx_)return nullptr;auto disabled=i2s_channel_disable(rx_);if(disabled!=ESP_OK&&disabled!=ESP_ERR_INVALID_STATE)return esp_err_to_name(disabled);auto removed=i2s_del_channel(rx_);if(removed!=ESP_OK)return esp_err_to_name(removed);rx_=nullptr;rate_=0;return nullptr;}
};
}
