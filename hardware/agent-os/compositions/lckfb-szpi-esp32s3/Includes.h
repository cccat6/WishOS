#include "drivers/esp32-services/SdMmcVolume.h"
#include "drivers/esp-wire-i2c/WireI2c.h"
#include "drivers/pca9557/Pca9557.h"
#include "drivers/qmi8658/Qmi8658.h"
#include "drivers/ft6336/Ft6336.h"
#include "drivers/ft6336/Ft6336Input.h"
#include "DisplayScheduler.h"
#include "Policy.h"
#include "PackedFont.h"
#include "UiMemory.h"
#include "UiSettings.h"
#include "display/NativeText.h"
#include "ButtonGestures.h"
#include <lvgl.h>
#include <lvgl_private.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>

#include "drivers/everest-duplex/EverestDuplex.h"
#include "audio/CompressedCapture.h"
#include "audio/Speech.h"
#include "drivers/esp-wakenet/EspWakeNet.h"
#include "drivers/energy-activity/EnergyActivity.h"

#include "drivers/esp-camera/EspCamera.h"

#include "BleConfiguration.h"
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>

#include <WishNimbleMemory.h>

#include "drivers/esp-lcd-spi/DmaCompletion.h"
#include "drivers/esp-lcd-spi/NativeExecutor.h"
#include "drivers/esp-lcd-spi/RasterFrame.h"

#include "drivers/esp-ledc-backlight/EspLedcBacklight.h"
#define WISHOS_LCD_BACKLIGHT_RESOURCE "/dev/backlight"

#define WISHOS_NATIVE_EARLY_INIT() backlight.holdOff()
