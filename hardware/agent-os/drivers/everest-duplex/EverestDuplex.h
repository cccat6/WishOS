#pragma once
#include <esp_attr.h>
#include "drivers/i2c/RegisterBus.h"
#include "audio/Playback.h"
#include "audio/CaptureFrame.h"
#include <WishCodecs.h>
#include <driver/i2s_std.h>
#include <freertos/task.h>
namespace notesos::audio {
// Codec control stays on the composition's shared I2C arbiter. The vendor codec
// implementation never creates a second Wire/I2C controller.
class CodecControl {
 struct Bridge {audio_codec_ctrl_if_t api{};drivers::I2cBus*bus;uint8_t address,port;};Bridge bridge_;
 static Bridge&get(const audio_codec_ctrl_if_t*p){return *reinterpret_cast<Bridge*>(const_cast<audio_codec_ctrl_if_t*>(p));}
 static int transfer(const audio_codec_ctrl_if_t*p,int reg,int len,void*data,int n,bool write){if(len!=1||n!=1||reg<0||reg>255)return ESP_CODEC_DEV_INVALID_ARG;auto&b=get(p);
  for(unsigned attempt=0;attempt<100;++attempt){drivers::I2cBus::Transaction tx(*b.bus);if(tx.held){auto e=write?tx.write(b.address,uint8_t(reg),*static_cast<uint8_t*>(data)):tx.read(b.address,uint8_t(reg),static_cast<uint8_t*>(data),1);return e?ESP_CODEC_DEV_WRITE_FAIL:ESP_CODEC_DEV_OK;}vTaskDelay(1);}return ESP_CODEC_DEV_WRITE_FAIL;
 }
public:
 CodecControl(drivers::I2cBus&bus,uint8_t address,uint8_t port):bridge_{{},&bus,address,port}{bridge_.api.is_open=[](const audio_codec_ctrl_if_t*){return true;};bridge_.api.read_reg=[](const audio_codec_ctrl_if_t*p,int r,int l,void*d,int n){return transfer(p,r,l,d,n,false);};bridge_.api.write_reg=[](const audio_codec_ctrl_if_t*p,int r,int l,void*d,int n){return transfer(p,r,l,d,n,true);};bridge_.api.get_info=[](const audio_codec_ctrl_if_t*p,audio_codec_ctrl_info_t*o)->int{auto&b=get(p);o->type=AUDIO_CODEC_CTRL_I2C;o->i2c.addr=uint16_t(b.address)<<1;o->i2c.port=b.port;return ESP_CODEC_DEV_OK;};}
 const audio_codec_ctrl_if_t*api()const{return &bridge_.api;}
};
// One owner of both I2S directions and clocks. Standard 32-bit stereo carries
// four 16-bit ADC slots (MIC1/MIC3, MIC2/MIC4). MIC3 is a wired playback reference, not a microphone.
class EverestDuplex final:public OutputBackend {
public:
 struct Wiring{int mclk,bclk,ws,input,output;unsigned port=0,i2cPort=0;uint8_t adcAddress=0x41,dacAddress=0x18;};
 using Amp=const char*(*)(bool);
private:
 Wiring pins_;Amp amp_;CodecControl adcCtrl_,dacCtrl_;const audio_codec_if_t*adc_=nullptr,*dac_=nullptr;i2s_chan_handle_t tx_=nullptr,rx_=nullptr;
 std::atomic<const char*>stage_{"idle"};std::atomic<uint32_t>sent_{0};uint32_t drainAfter_=0;unsigned rate_=0,channels_=1;bool outputOpen_=false;int32_t converted_[320]{};
 static bool IRAM_ATTR sent(i2s_chan_handle_t,i2s_event_data_t*,void*p){static_cast<EverestDuplex*>(p)->sent_.fetch_add(1);return false;}
 void deleteCodec(const audio_codec_if_t*&p){if(p){p->close(p);free(const_cast<audio_codec_if_t*>(p));p=nullptr;}}
public:
 EverestDuplex(drivers::I2cBus&bus,Wiring pins,Amp amp):pins_(pins),amp_(amp),adcCtrl_(bus,pins.adcAddress,pins.i2cPort),dacCtrl_(bus,pins.dacAddress,pins.i2cPort){}
 const char*stage()const{return stage_.load();}
 void describe(JsonObject o)const override{o["adapter"]="es7210-es8311-shared-i2s";o["channels"]="1,2";o["pcm_bits"]=16;o["capture_roles"]="MMR";o["duplex"]=true;o["shared_clock_rule"]="all_active_consumers_use_same_sample_rate";o["completion_basis"]="dma_ring_drained";auto rates=o["sample_rates_hz"].to<JsonArray>();for(unsigned r:{8000,16000,24000,32000,44100,48000})rates.add(r);}
 const char*validate(PcmFormat f)const override{if(f.channels!=1&&f.channels!=2)return "audio_channels";for(unsigned r:{8000,16000,24000,32000,44100,48000})if(r==f.rate)return nullptr;return "audio_sample_rate";}
 // Called only on the native audio worker, after admission.
 const char*initialize(unsigned rate){shutdown();if(auto e=amp_(false))return e;i2s_chan_config_t config=I2S_CHANNEL_DEFAULT_CONFIG(i2s_port_t(pins_.port),I2S_ROLE_MASTER);config.dma_desc_num=4;config.dma_frame_num=160;config.auto_clear_after_cb=true;
  stage_="i2s_allocate";auto e=i2s_new_channel(&config,&tx_,&rx_);if(e!=ESP_OK)return esp_err_to_name(e);
  i2s_std_config_t s{};s.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(rate);s.clk_cfg.mclk_multiple=I2S_MCLK_MULTIPLE_256;s.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO);s.gpio_cfg.mclk=gpio_num_t(pins_.mclk);s.gpio_cfg.bclk=gpio_num_t(pins_.bclk);s.gpio_cfg.ws=gpio_num_t(pins_.ws);s.gpio_cfg.dout=gpio_num_t(pins_.output);s.gpio_cfg.din=gpio_num_t(pins_.input);
  stage_="i2s_initialize";if((e=i2s_channel_init_std_mode(tx_,&s))!=ESP_OK||(e=i2s_channel_init_std_mode(rx_,&s))!=ESP_OK){shutdown();return esp_err_to_name(e);}i2s_event_callbacks_t callbacks{};callbacks.on_sent=sent;
  stage_="i2s_callback";if((e=i2s_channel_register_event_callback(tx_,&callbacks,this))!=ESP_OK){shutdown();return esp_err_to_name(e);}
  // Keep clocks present while initializing and synchronizing both codecs.
  stage_="i2s_enable";if((e=i2s_channel_enable(tx_))!=ESP_OK||(e=i2s_channel_enable(rx_))!=ESP_OK){shutdown();return esp_err_to_name(e);}
  stage_="codec_initialize";es7210_codec_cfg_t input{};input.ctrl_if=adcCtrl_.api();input.mic_selected=ES7210_SEL_MIC1|ES7210_SEL_MIC2|ES7210_SEL_MIC3;input.mclk_div=256;adc_=es7210_codec_new(&input);
  es8311_codec_cfg_t output{};output.ctrl_if=dacCtrl_.api();output.codec_mode=ESP_CODEC_DEV_WORK_MODE_DAC;output.pa_pin=-1;output.use_mclk=true;output.mclk_div=256;dac_=es8311_codec_new(&output);
  if(!adc_||!dac_){shutdown();return "codec_init_failed";}
  esp_codec_dev_sample_info_t adcFormat{};adcFormat.sample_rate=rate;adcFormat.channel=2;adcFormat.bits_per_sample=32;adcFormat.mclk_multiple=256;
  esp_codec_dev_sample_info_t dacFormat=adcFormat;dacFormat.bits_per_sample=16;
  stage_="codec_format";int r=adc_->set_fs(adc_,&adcFormat)|dac_->set_fs(dac_,&dacFormat);// Enabling the ADC reselects microphones and resets gain registers.
  r|=adc_->enable(adc_,true);r|=adc_->set_mic_channel_gain(adc_,3,24);r|=adc_->set_mic_channel_gain(adc_,4,0);r|=dac_->set_vol(dac_,-12);r|=dac_->enable(dac_,true);r|=dac_->mute(dac_,false);
  if(r){shutdown();return "codec_format_failed";}rate_=rate;sent_=0;drainAfter_=0;stage_="synchronizing";size_t discarded=0;while(discarded<rate/20){size_t got=0;auto error=i2s_channel_read(rx_,converted_,sizeof converted_,&got,100);if(error!=ESP_OK&&error!=ESP_ERR_TIMEOUT){shutdown();return esp_err_to_name(error);}if(!got){shutdown();return "audio_sync_timeout";}discarded+=got/8;}stage_="ready";return nullptr;
 }
 const char*receive(int16_t*out,size_t capacity,size_t&frames){frames=0;int32_t raw[320];size_t got=0;auto e=i2s_channel_read(rx_,raw,sizeof raw,&got,10);if(e!=ESP_OK&&e!=ESP_ERR_TIMEOUT)return esp_err_to_name(e);frames=std::min(capacity,got/8);for(size_t i=0;i<frames;++i){out[i*3]=int16_t(uint32_t(raw[i*2])>>16);out[i*3+1]=int16_t(uint32_t(raw[i*2+1])>>16);out[i*3+2]=int16_t(raw[i*2]);}return nullptr;}
 const char*open(PcmFormat f)override{if(auto e=validate(f))return e;if(!rate_||rate_!=f.rate)return "capture_format_conflict";channels_=f.channels;sent_=0;drainAfter_=0;if(auto e=amp_(true))return e;outputOpen_=true;return nullptr;}
 const char*write(const int16_t*p,size_t n,size_t&accepted)override{accepted=0;size_t frames=std::min<size_t>(n/channels_,160);for(size_t i=0;i<frames;++i){converted_[2*i]=int32_t(p[i*channels_])*65536;converted_[2*i+1]=int32_t(p[i*channels_+(channels_==2)])*65536;}size_t bytes=0;auto e=i2s_channel_write(tx_,converted_,frames*8,&bytes,0);accepted=(bytes/8)*channels_;if(bytes)drainAfter_=sent_.load()+5;return e==ESP_OK||e==ESP_ERR_TIMEOUT?nullptr:esp_err_to_name(e);}
 bool drained()override{return int32_t(sent_.load()-drainAfter_)>=0;}
 void close()override{amp_(false);outputOpen_=false;}
 void shutdown(){close();deleteCodec(adc_);deleteCodec(dac_);if(rx_){i2s_channel_disable(rx_);i2s_del_channel(rx_);rx_=nullptr;}if(tx_){i2s_channel_disable(tx_);i2s_del_channel(tx_);tx_=nullptr;}rate_=0;stage_="idle";}
};
}
