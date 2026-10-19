#pragma once
#include "FreeRTOS.h"
#include <mutex>
#include <condition_variable>
#include <cassert>
struct StaticSemaphore_t {std::mutex mutex;std::condition_variable changed;bool ready=false,waiting=false;};
using SemaphoreHandle_t=StaticSemaphore_t*;
inline bool lcdStubCreateFailure=false;inline std::atomic<unsigned>lcdStubWaiting{0};
inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t*p){if(lcdStubCreateFailure)return nullptr;p->ready=false;return p;}
inline BaseType_t xSemaphoreTake(SemaphoreHandle_t p,TickType_t wait){std::unique_lock<std::mutex>lock(p->mutex);if(!wait&&!p->ready)return pdFALSE;if(wait){assert(wait==portMAX_DELAY);p->waiting=true;++lcdStubWaiting;p->changed.wait(lock,[&]{return p->ready;});--lcdStubWaiting;p->waiting=false;}p->ready=false;return pdTRUE;}
inline BaseType_t xSemaphoreGiveFromISR(SemaphoreHandle_t p,BaseType_t*woken){std::lock_guard<std::mutex>lock(p->mutex);*woken=p->waiting?pdTRUE:pdFALSE;bool was=p->ready;p->ready=true;p->changed.notify_all();return was?pdFALSE:pdTRUE;}
