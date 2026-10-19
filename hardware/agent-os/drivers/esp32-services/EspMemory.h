#pragma once
#include "Core.h"
#include "BoardPackage.h"
#include <esp_heap_caps.h>
namespace notesos {
inline const char* configureEspMemory(Core&core){
 auto&memory=core.resources();
 constexpr uint32_t internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
 constexpr uint32_t external=MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT;
 memory.heap(ResourceBudget::Region::Internal,{
  []()->size_t{return heap_caps_get_total_size(internal);},
  []()->size_t{return heap_caps_get_free_size(internal);},
  []()->size_t{return heap_caps_get_largest_free_block(internal);},
  [](void*p,size_t n)->void*{return heap_caps_realloc(p,n,internal);}
 });
 memory.heap(ResourceBudget::Region::External,{
  []()->size_t{return heap_caps_get_total_size(external);},
  []()->size_t{return heap_caps_get_free_size(external);},
  []()->size_t{return heap_caps_get_largest_free_block(external);},
  [](void*p,size_t n)->void*{return heap_caps_realloc(p,n,external);}
 });
 // This journal is kernel-lifetime data, previously part of Core's static BSS.
 // DMA/ISR buffers keep their own internal-capability allocation requirements.
 if(heap_caps_get_total_size(external))core.eventStorage([](size_t n)->void*{return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);},heap_caps_free);
 // The package generator validates these native startup defaults.
 JsonDocument defaults;deserializeJson(defaults,boardpack::RESOURCE_DEFAULTS);
 return memory.configure(defaults.as<JsonObjectConst>());
}
}
