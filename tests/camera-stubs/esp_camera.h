#pragma once
#include <cstdint>
#include <cstddef>
#include <sys/time.h>
#include <thread>
#include <vector>
#include <atomic>
using esp_err_t=int;
constexpr int ESP_OK=0,ESP_ERR_INVALID_STATE=1,ESP_FAIL=2,pdPASS=1;
inline unsigned pdMS_TO_TICKS(unsigned n){return n;}
extern std::atomic<uint64_t>cameraClock;
extern std::vector<std::thread>cameraTasks;
inline uint32_t millis(){return cameraClock.load();}
inline void vTaskDelay(unsigned n){cameraClock+=n;std::this_thread::yield();}
inline int xTaskCreate(void(*run)(void*),const char*,unsigned,void*p,unsigned,void*){cameraTasks.emplace_back(run,p);return pdPASS;}
inline void vTaskDelete(void*){}
inline const char*esp_err_to_name(int){return "sdk_error";}
enum pixformat_t{PIXFORMAT_RGB565,PIXFORMAT_JPEG};
enum framesize_t{FRAMESIZE_QQVGA,FRAMESIZE_QVGA,FRAMESIZE_VGA};
enum camera_fb_location_t{CAMERA_FB_IN_PSRAM,CAMERA_FB_IN_DRAM};
struct camera_config_t{int xclk_freq_hz=24000000;pixformat_t pixel_format=PIXFORMAT_RGB565;framesize_t frame_size=FRAMESIZE_QVGA;size_t fb_count=1;camera_fb_location_t fb_location=CAMERA_FB_IN_PSRAM;};
struct camera_fb_t{uint8_t*buf;size_t len,width,height;pixformat_t format;timeval timestamp;};
struct sensor_t{struct{uint16_t PID=0x9b;}id;int(*set_hmirror)(sensor_t*,int);int(*set_vflip)(sensor_t*,int);int(*set_colorbar)(sensor_t*,int);int(*set_exposure_ctrl)(sensor_t*,int);int(*set_whitebal)(sensor_t*,int);int(*set_gain_ctrl)(sensor_t*,int);};
int esp_camera_set_psram_mode(bool);bool esp_camera_get_psram_mode();int esp_camera_init(const camera_config_t*);int esp_camera_deinit();sensor_t*esp_camera_sensor_get();camera_fb_t*esp_camera_fb_get();void esp_camera_fb_return(camera_fb_t*);
