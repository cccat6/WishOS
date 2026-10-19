#pragma once
#include <driver/spi_common.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <new>
#include <stdint.h>
#ifndef WISHOS_LCD_NATIVE_CORE
#define WISHOS_LCD_NATIVE_CORE (configNUMBER_OF_CORES > 1 ? 1 : 0)
#endif
namespace notesos::drivers {
inline constexpr int LcdNativeCore=WISHOS_LCD_NATIVE_CORE;
static_assert(LcdNativeCore>=0&&LcdNativeCore<configNUMBER_OF_CORES&&LcdNativeCore<=1,"LCD native core must select an available explicit SPI interrupt core");
// SPI polling/queued transactions and their ISR share one native execution
// core. This is a driver invariant, independent of runtime application affinity.
inline void lcdSpiInterruptAffinity(spi_bus_config_t&bus){bus.isr_cpu_id=LcdNativeCore==0?ESP_INTR_CPU_AFFINITY_0:ESP_INTR_CPU_AFFINITY_1;}
class LcdNativeExecutor {
 struct Call{void(*run)(void*);void*context;};
 static void entry(void*p){auto call=*static_cast<Call*>(p);delete static_cast<Call*>(p);call.run(call.context);vTaskDelete(nullptr);}
public:
 static bool launch(void(*run)(void*),void*context,const char*name,uint32_t stackBytes){auto call=new(std::nothrow)Call{run,context};if(!call)return false;if(xTaskCreatePinnedToCore(entry,name,stackBytes,call,1,nullptr,LcdNativeCore)!=pdPASS){delete call;return false;}return true;}
};
}
