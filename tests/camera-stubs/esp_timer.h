#pragma once
#include <atomic>
#include <cstdint>
extern std::atomic<uint64_t>cameraClock;
inline int64_t esp_timer_get_time(){return int64_t(cameraClock.load())*1000;}
