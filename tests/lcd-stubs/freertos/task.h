#pragma once
#include "FreeRTOS.h"
#include <string>
using TaskFunction_t=void(*)(void*);using TaskHandle_t=void*;
inline bool lcdStubTaskFailure=false;inline int lcdStubTaskCore=-1,lcdStubRunningCore=0;inline uint32_t lcdStubTaskStack=0;inline unsigned lcdStubTaskPriority=0,lcdStubTaskDeletes=0;inline std::string lcdStubTaskName;inline TaskFunction_t lcdStubTaskEntry=nullptr;inline void*lcdStubTaskContext=nullptr;
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t entry,const char*name,uint32_t stack,void*context,unsigned priority,TaskHandle_t*,BaseType_t core){if(lcdStubTaskFailure)return pdFALSE;lcdStubTaskEntry=entry;lcdStubTaskContext=context;lcdStubTaskCore=core;lcdStubTaskStack=stack;lcdStubTaskPriority=priority;lcdStubTaskName=name;return pdPASS;}
inline void vTaskDelete(TaskHandle_t){++lcdStubTaskDeletes;}
inline void lcdStubRunTask(){auto entry=lcdStubTaskEntry;auto context=lcdStubTaskContext;lcdStubTaskEntry=nullptr;lcdStubTaskContext=nullptr;lcdStubRunningCore=lcdStubTaskCore;entry(context);}
