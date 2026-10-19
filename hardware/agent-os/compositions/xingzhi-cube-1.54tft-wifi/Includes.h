#include "DisplayScheduler.h"
#include "Policy.h"
#include "UiMemory.h"
#include "UiSettings.h"
#include "PackedFont.h"
#include "display/NativeText.h"
#include "ButtonGestures.h"
#include <lvgl.h>
#include <lvgl_private.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>
#include "audio/Playback.h"
#include "audio/CompressedCapture.h"
#include "audio/Speech.h"
#include "drivers/esp-i2s-input/EspI2sInput.h"
#include "drivers/esp-i2s-output/EspI2sOutput.h"
#include "drivers/energy-activity/EnergyActivity.h"

#include <driver/rtc_io.h>
#include <esp_adc/adc_oneshot.h>
#include "BleConfiguration.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <WishNimbleMemory.h>

#include "drivers/esp-wakenet/EspWakeNet.h"

#include "drivers/esp-lcd-spi/DmaCompletion.h"
#include "drivers/esp-lcd-spi/NativeExecutor.h"
#include "drivers/esp-lcd-spi/RasterFrame.h"

#include "drivers/esp-ledc-backlight/EspLedcBacklight.h"
#define WISHOS_LCD_BACKLIGHT_RESOURCE "/dev/backlight"

#define WISHOS_NATIVE_EARLY_INIT() backlight.holdOff()
