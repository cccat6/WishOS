struct WorkerLaunch{void(*run)(void*);void*arg;};
static constexpr size_t WORKER_STACK_BYTES=8192;
static bool launchWorker(void(*run)(void*),void*arg,unsigned cpu){
 auto launch=new(std::nothrow)WorkerLaunch{run,arg};if(!launch)return false;
 auto entry=[](void*p){auto call=*(WorkerLaunch*)p;delete (WorkerLaunch*)p;call.run(call.arg);vTaskDelete(nullptr);};
 if(xTaskCreatePinnedToCore(entry,"notes-compute",WORKER_STACK_BYTES,launch,1,nullptr,cpu)!=pdPASS){delete launch;return false;}return true;
}
static void yieldWorker(void*){vTaskDelay(1);}
static bool admitWorker(size_t bytes){return core.resources().canAllocate(bytes);}
// IDF dynamically allocates the declared stack and TCB in internal RAM. TLS
// is carved from that stack. General system headroom remains ResourceBudget's
// configured heap reserve rather than a second per-worker allowance.
static Workers workers(core,vfs,{CONFIG_FREERTOS_NUMBER_OF_CORES,launchWorker,yieldWorker,admitWorker,WORKER_STACK_BYTES+sizeof(StaticTask_t)+sizeof(WorkerLaunch)});
static const char*workerHandler(const char*m,JsonObjectConst a,JsonObject out){return workers.invoke(m,a,out);}
