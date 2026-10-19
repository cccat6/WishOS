// Rendering is shared WishOS UI; ST7789 encoding, transport and completion are
// native. The callback only signals DMA completion and never dispatches apps.
static esp_lcd_panel_handle_t lcdPanel=nullptr;
static esp_lcd_panel_io_handle_t lcdIo=nullptr;
static drivers::LcdDmaCompletion lcdCompletion;
static bool lcdReady=false;
static char lcdInitError[80]{};
static NativeOperation lcdInitialization(core,"/dev/display",{[](void(*run)(void*),void*p){return drivers::LcdNativeExecutor::launch(run,p,"wish-lcd-init",8192);},12288},nullptr,[](void*,const std::atomic<bool>&)->const char*{
 if(!lcdCompletion.initialize())return "lcd_completion_init_failed";
 if(auto error=expander.configure(7,7,5)){copy(lcdInitError,sizeof lcdInitError,error);return error;}
 spi_bus_config_t bus{};bus.sclk_io_num=pins::LCD_SCLK;bus.mosi_io_num=pins::LCD_MOSI;bus.miso_io_num=-1;bus.quadwp_io_num=-1;bus.quadhd_io_num=-1;bus.max_transfer_sz=boardpack::DISPLAY_WIDTH*drivers::LcdDmaRows*2;
 drivers::lcdSpiInterruptAffinity(bus);
 esp_err_t e=spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO);bool busReady=e==ESP_OK;
 if(e==ESP_OK){esp_lcd_panel_io_spi_config_t io{};io.dc_gpio_num=pins::LCD_DC;io.cs_gpio_num=-1;io.pclk_hz=pins::LCD_SPI_HZ;io.lcd_cmd_bits=8;io.lcd_param_bits=8;io.spi_mode=2;io.trans_queue_depth=1;io.user_ctx=&lcdCompletion;io.on_color_trans_done=[](esp_lcd_panel_io_handle_t,esp_lcd_panel_io_event_data_t*,void*p)->bool{return static_cast<drivers::LcdDmaCompletion*>(p)->fromIsr();};e=esp_lcd_new_panel_io_spi(SPI3_HOST,&io,&lcdIo);}
 if(e==ESP_OK){esp_lcd_panel_dev_config_t config{};config.reset_gpio_num=-1;config.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_RGB;config.bits_per_pixel=16;e=esp_lcd_new_panel_st7789(lcdIo,&config,&lcdPanel);}
 if(e==ESP_OK)e=esp_lcd_panel_reset(lcdPanel);
 if(e==ESP_OK){if(auto error=expander.write(1,0)){copy(lcdInitError,sizeof lcdInitError,error);e=ESP_FAIL;}}
 if(e==ESP_OK)e=esp_lcd_panel_init(lcdPanel);if(e==ESP_OK)e=esp_lcd_panel_invert_color(lcdPanel,true);if(e==ESP_OK)e=esp_lcd_panel_swap_xy(lcdPanel,true);if(e==ESP_OK)e=esp_lcd_panel_mirror(lcdPanel,true,false);if(e==ESP_OK)e=esp_lcd_panel_disp_on_off(lcdPanel,true);
 if(e!=ESP_OK){if(lcdPanel)esp_lcd_panel_del(lcdPanel);if(lcdIo)esp_lcd_panel_io_del(lcdIo);lcdPanel=nullptr;lcdIo=nullptr;if(busReady)spi_bus_free(SPI3_HOST);if(!*lcdInitError)copy(lcdInitError,sizeof lcdInitError,esp_err_to_name(e));return lcdInitError;}
 return nullptr;
},[](void*){JsonDocument status;lcdInitialization.read(status.to<JsonObject>());copy(lcdInitError,sizeof lcdInitError,status["error"]|"");lcdReady=lcdPanel&&!*lcdInitError;backlight.panelReady(lcdReady);backlight.poll(policies);});
static void startLcdInit(){JsonDocument result;if(auto e=lcdInitialization.start("initialize",result.to<JsonObject>()))copy(lcdInitError,sizeof lcdInitError,e);}
static constexpr DisplayComponent lcdComponent{"st7789","lcd","spi",boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT,DisplayEvidence::TransportComplete,"spi_dma","on_demand"};
#include "drivers/esp-lcd-spi/Presentation.inc"
