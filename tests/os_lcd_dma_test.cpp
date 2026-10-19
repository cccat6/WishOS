#include "drivers/esp-lcd-spi/DmaCompletion.h"
#include <cassert>
#include <thread>
#include <iostream>
using notesos::drivers::LcdDmaCompletion;
int main(){
 LcdDmaCompletion completion;lcdStubCreateFailure=true;assert(!completion.initialize());lcdStubCreateFailure=false;assert(completion.initialize());assert(completion.initialize());
 // A callback can run before draw_bitmap returns, before the worker waits.
 completion.prepare();assert(!completion.fromIsr());completion.wait();assert(lcdStubYields==0);
 for(unsigned i=0;i<8;++i){
  // A leftover completed signal is explicitly consumed before a new submit.
  completion.fromIsr();completion.prepare();std::atomic<bool>returned{false};
  std::thread worker([&]{completion.wait();returned=true;});while(!lcdStubWaiting&&!returned)std::this_thread::yield();
  assert(!returned);
  completion.fromIsr();worker.join();assert(returned);
 }
 assert(lcdStubYields>0); // ISR requests immediate scheduling, without tick polling.
 // The signal belongs to the device and survives short-lived native workers.
 completion.prepare();completion.fromIsr();completion.wait();
 std::cout<<"LCD binary completion: initialization rejection, early IRQ, delayed IRQ, stale signal drain and worker replacement passed\n";
}
