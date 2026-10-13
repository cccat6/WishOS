struct WorkerLaunch{void(*run)(void*);void*arg;};
static bool launchWorker(void(*run)(void*),void*arg,unsigned cpu){
 auto launch=new(std::nothrow)WorkerLaunch{run,arg};if(!launch)return false;
 auto entry=[](void*p){auto call=*(WorkerLaunch*)p;delete (WorkerLaunch*)p;call.run(call.arg);vTaskDelete(nullptr);};
 if(xTaskCreatePinnedToCore(entry,"notes-compute",8192,launch,1,nullptr,cpu)!=pdPASS){delete launch;return false;}return true;
}
static void yieldWorker(void*){vTaskDelay(1);}
static bool admitWorker(size_t bytes){return core.resources().canAllocate(bytes);}
static Workers workers(core,vfs,{CONFIG_FREERTOS_NUMBER_OF_CORES,launchWorker,yieldWorker,admitWorker});
static const char*workerHandler(const char*m,JsonObjectConst a,JsonObject out){return workers.invoke(m,a,out);}
