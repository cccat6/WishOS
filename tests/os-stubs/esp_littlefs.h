#pragma once
constexpr int ESP_OK=0;
extern int flashFormatStatus;
inline int esp_littlefs_format(const char*){return flashFormatStatus;}
