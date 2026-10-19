#pragma once
// Fixed Arduino ESP32 3.3.11 ESP32-S3 SDK setting used by the real adapter.
#define CONFIG_CAMERA_DMA_BUFFER_SIZE_MAX 32768

#ifndef CONFIG_IDF_TARGET_ESP32S3
#define CONFIG_IDF_TARGET_ESP32S3 1
#endif
