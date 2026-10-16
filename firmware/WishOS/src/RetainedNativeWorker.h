#pragma once
#include "ResourceBudget.h"
#include <condition_variable>
namespace notesos {
// Compiled services can retain one bounded native executor across SDK lifetime.
// Cleanup then uses the same stack as initialization, even when admitting a new
// task would fail. This executor never dispatches resources or application code.
class RetainedNativeWorker {
public:
 struct Executor{bool(*launch)(void(*)(void*),void*)=nullptr;size_t memoryBytes=0;};
private:
 ResourceBudget&memory_;Executor executor_;std::mutex mutex_;std::condition_variable changed_;
 bool live_=false,queued_=false,running_=false,closing_=false;uint32_t reservation_=0;void(*run_)(void*)=nullptr;void*context_=nullptr;
 static void loop(void*p){auto&s=*static_cast<RetainedNativeWorker*>(p);std::unique_lock<std::mutex>lock(s.mutex_);
  for(;;){s.changed_.wait(lock,[&]{return s.queued_||s.closing_;});if(s.closing_&&!s.queued_)break;auto run=s.run_;auto context=s.context_;s.queued_=false;s.running_=true;lock.unlock();run(context);lock.lock();s.running_=false;s.changed_.notify_all();}
  s.memory_.release(s.reservation_);s.reservation_=0;s.live_=false;s.changed_.notify_all();
 }
public:
 RetainedNativeWorker(ResourceBudget&memory,Executor executor):memory_(memory),executor_(executor){}
 ~RetainedNativeWorker(){shutdown();}
 bool submit(void(*run)(void*),void*context){std::lock_guard<std::mutex>lock(mutex_);if(closing_||queued_||!run)return false;
  if(!live_){if(!executor_.launch)return false;reservation_=memory_.acquire(executor_.memoryBytes,0,ResourceBudget::Region::Internal);if(!reservation_)return false;live_=true;
   if(!executor_.launch(loop,this)){live_=false;memory_.release(reservation_);reservation_=0;return false;}memory_.used(reservation_,executor_.memoryBytes);
  }
  run_=run;context_=context;queued_=true;changed_.notify_one();return true;
 }
 bool idle(){std::lock_guard<std::mutex>lock(mutex_);return !queued_&&!running_;}
 // Native service shutdown only; application replacement does not destroy its
 // cleanup executor. Platform owners join/reclaim the underlying task afterward.
 void shutdown(){std::unique_lock<std::mutex>lock(mutex_);if(!live_)return;closing_=true;changed_.notify_all();changed_.wait(lock,[&]{return !live_;});closing_=false;}
};
}
