#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
namespace notesos::drivers {
inline constexpr unsigned LcdDmaRows=8;
// One serial color transaction is outstanding. This permanent device-owned
// signal outlives each native transfer task and uses no notification slot/heap.
class LcdDmaCompletion {
 StaticSemaphore_t storage_{};SemaphoreHandle_t signal_=nullptr;
public:
 bool initialize(){if(!signal_)signal_=xSemaphoreCreateBinaryStatic(&storage_);return signal_!=nullptr;}
 void prepare(){xSemaphoreTake(signal_,0);}
 void wait(){xSemaphoreTake(signal_,portMAX_DELAY);}
 bool fromIsr(){BaseType_t woken=pdFALSE;xSemaphoreGiveFromISR(signal_,&woken);
  // IDF 5.5.5 SPI's post callback ignores on_color_trans_done's bool result.
  if(woken==pdTRUE)portYIELD_FROM_ISR();return woken==pdTRUE;
 }
};
}
