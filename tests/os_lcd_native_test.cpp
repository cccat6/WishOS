#include "drivers/esp-lcd-spi/NativeExecutor.h"
#include <cassert>
#include <iostream>
using namespace notesos::drivers;
static_assert(LcdNativeCore==TEST_EXPECT_CORE);
struct Invocation{spi_bus_config_t bus{};unsigned calls=0;};
int main(){Invocation invocation;lcdSpiInterruptAffinity(invocation.bus);auto irqCore=int(invocation.bus.isr_cpu_id)-1;assert(irqCore==LcdNativeCore);auto work=[](void*p){auto&state=*static_cast<Invocation*>(p);assert(lcdStubRunningCore==int(state.bus.isr_cpu_id)-1);++state.calls;};
 for(auto name:{"wish-lcd-init","wish-lcd-tx"}){unsigned stack=name[9]=='i'?8192:4096;lcdStubRunningCore=configNUMBER_OF_CORES-1-LcdNativeCore;assert(LcdNativeExecutor::launch(work,&invocation,name,stack));assert(lcdStubTaskCore==LcdNativeCore&&lcdStubTaskName==name&&lcdStubTaskStack==stack&&lcdStubTaskPriority==1);lcdStubRunningCore=configNUMBER_OF_CORES-1-LcdNativeCore;lcdStubRunTask();}
 assert(invocation.calls==2&&lcdStubTaskDeletes==2&&!lcdStubTaskEntry);lcdStubTaskFailure=true;assert(!LcdNativeExecutor::launch(work,&invocation,"failed",4096));assert(invocation.calls==2&&!lcdStubTaskEntry&&!lcdStubTaskContext);std::cout<<"LCD explicit task/IRQ same core="<<LcdNativeCore<<", cores="<<configNUMBER_OF_CORES<<"; init, transfer, migration and launch rejection passed\n";
}
