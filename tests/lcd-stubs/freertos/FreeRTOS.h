#pragma once
#include <atomic>
#include <cstdint>
#ifndef configNUMBER_OF_CORES
#define configNUMBER_OF_CORES 2
#endif
using BaseType_t=int;using TickType_t=uint32_t;
constexpr BaseType_t pdTRUE=1,pdFALSE=0,pdPASS=1;constexpr TickType_t portMAX_DELAY=UINT32_MAX;
inline std::atomic<unsigned>lcdStubYields{0};
#define portYIELD_FROM_ISR() (++lcdStubYields)
