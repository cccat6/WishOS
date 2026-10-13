#include "DisplayScheduler.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace notesos;
struct Frame:DisplayRequest {unsigned width=0,height=0;};
struct TestAdapter {
 enum Type{Immediate,Dma,Epd,Scanout}type;
 PresentationState&views;std::vector<JsonDocument>receipts;
 unsigned starts=0,polls=0,leases=0,disposed=0,suspends=0;
 bool defer=false,reject=false,ready=true,done=false,failComplete=false,nativeReadsSource=false,scanoutRunning=false;
 explicit TestAdapter(Type t,PresentationState&v):type(t),views(v){}
 bool available()const{return ready;}
 DisplayBegin begin(Frame&,uint64_t){++starts;++leases;if(defer)return DisplayBegin::deferred();if(reject)return DisplayBegin::failed("source_unavailable");nativeReadsSource=type==Dma;return {};}
 DisplayOutcome poll(Frame&,uint64_t){
  ++polls;if(type!=Immediate&&!done)return {};
  nativeReadsSource=false;
  if(failComplete){ready=false;return DisplayOutcome::failed("native_transfer_failed");}
  if(type==Scanout){scanoutRunning=true;return DisplayOutcome::success(DisplayEvidence::FrameLatched,"vsync_framebuffer_latch");}
  return DisplayOutcome::success(type==Epd?DisplayEvidence::DeviceReady:DisplayEvidence::TransportComplete,type==Epd?"busy_inactive":"transfer_callback");
 }
 void suspend(Frame&){assert(leases);--leases;++suspends;}
 void completed(Frame&f,const DisplayOutcome&o,uint64_t elapsed){if(f.id==begunId)assert(!nativeReadsSource);JsonDocument d;o.describe(f,d.to<JsonObject>(),elapsed);assert(measureJson(d)<MAX_EVENT);receipts.push_back(std::move(d));}
 void dispose(Frame&f){++disposed;if(f.id==begunId&&leases)--leases;}
 uint32_t begunId=0;
};
// Real adapter wrapper keeps the leased identity distinct from queued requests.
struct Adapter:TestAdapter {
 using TestAdapter::TestAdapter;
 DisplayBegin begin(Frame&f,uint64_t now){begunId=f.id;return TestAdapter::begin(f,now);}
};
static Frame frame(PresentationState&v,uint32_t view,unsigned id,unsigned w=128,unsigned h=64){Frame r;r.id=id;r.owner=v.get(view)->owner;r.stamp=v.stamp(view);r.width=w;r.height=h;return r;}
int main(){
 for(auto type:{TestAdapter::Immediate,TestAdapter::Dma,TestAdapter::Epd,TestAdapter::Scanout}){
  PresentationState views;uint32_t view;assert(!views.create(47,true,false,view));assert(!views.activate(view,false,47));
  Adapter adapter(type,views);DisplayScheduler<Frame,Adapter>q(views,adapter);using Policy=decltype(q)::Policy;
  assert(!q.enqueue(frame(views,view,1,800,480),Policy::Latest,2));assert(adapter.starts==0);q.tick(100);
  if(type==TestAdapter::Immediate){assert(!q.active()&&adapter.receipts.size()==1);assert(adapter.receipts.back()["total_ms"]==0);}
  else{
   assert(q.active()&&adapter.leases==1&&adapter.receipts.empty());
   assert(!views.revise(view,47,0));assert(!q.enqueue(frame(views,view,2),Policy::Latest,2));
   assert(!views.revise(view,47,0));assert(!q.enqueue(frame(views,view,3),Policy::Latest,2));
   assert(q.pending()==1&&adapter.receipts.size()==1&&adapter.receipts[0]["job_id"]==2&&adapter.receipts[0]["state"]=="superseded");
   // A new page cannot interrupt outstanding DMA, an EPD cycle or scanout latch.
   q.tick(110);assert(q.active()&&adapter.starts==1&&adapter.leases==1);
   adapter.done=true;q.tick(200);assert(!q.active()&&adapter.leases==0&&views.presented().revision==1);
   q.tick(201);assert(!q.active()&&q.pending()==0&&adapter.starts==2&&views.presented().revision==3);
  }
  for(auto&r:adapter.receipts)assert(r["owner"]==47&&r["view_id"]==view&&r["activation"].as<unsigned>()>0);
  auto&last=adapter.receipts.back();assert(last["completion"]["kind"]==(type==TestAdapter::Epd?"device_ready":type==TestAdapter::Scanout?"frame_latched":"transport_complete"));
  if(type==TestAdapter::Scanout)assert(adapter.scanoutRunning); // receipt ends a job, not continuous refresh
  assert(adapter.leases==0);
 }
 // Driver preparation can temporarily yield a bus without losing its request.
 PresentationState views;uint32_t view,other;assert(!views.create(9,true,false,view));assert(!views.create(9,true,false,other));assert(!views.activate(view,false,9));
 Adapter adapter(TestAdapter::Dma,views);DisplayScheduler<Frame,Adapter,2>q(views,adapter);using Policy=decltype(q)::Policy;
 adapter.defer=true;assert(!q.enqueue(frame(views,view,1),Policy::Fifo,2));q.tick(0);assert(!q.active()&&q.pending()==1&&adapter.leases==0&&adapter.suspends==1&&adapter.receipts.empty());
 adapter.defer=false;adapter.reject=true;q.tick(1);assert(!q.active()&&q.pending()==0&&adapter.leases==0&&adapter.receipts.back()["error"]=="source_unavailable");assert(views.presented().view==0);
 adapter.reject=false;assert(!q.enqueue(frame(views,view,2),Policy::Fifo,2));assert(!q.enqueue(frame(views,view,3),Policy::Fifo,2));
 auto n=adapter.receipts.size();assert(q.enqueue(frame(views,view,4),Policy::Latest,0));assert(q.enqueue(frame(views,view,4),Policy::Fifo,2));assert(q.enqueue(frame(views,view,4),Policy::Reject,2));assert(q.pending()==2&&adapter.receipts.size()==n);
 // FIFO can retain accepted revisions, but navigation invalidates queued pages.
 assert(!views.activate(other,false,9));q.tick(2);assert(q.pending()==0&&adapter.starts==2&&adapter.receipts.size()==n+2);
 assert(adapter.receipts.back()["state"]=="superseded"&&adapter.leases==0);
 // A terminal transfer failure releases its source once, blocks further native
 // work while the adapter is unavailable, and preserves the last presented view.
 assert(!q.enqueue(frame(views,other,5),Policy::Fifo,2));q.tick(10);assert(q.active()&&adapter.nativeReadsSource);
 assert(!q.enqueue(frame(views,other,6),Policy::Fifo,2));adapter.failComplete=true;adapter.done=true;q.tick(20);
 assert(!q.active()&&adapter.leases==0&&q.pending()==1&&views.presented().view==0);auto failed=adapter.receipts.size();
 q.tick(21);assert(adapter.receipts.size()==failed&&q.pending()==1);adapter.failComplete=false;adapter.ready=true;q.tick(22);assert(q.pending()==0&&!q.active()&&views.presented().view==other);
 // A headless runtime can host other resources without constructing a display.
 // Component metadata contains actual geometry/protocol, not an E6 frame contract.
 DisplayComponent mono{"ssd1306","oled","i2c",128,64,DisplayEvidence::TransportComplete,"i2c_transfer","continuous"};JsonDocument description;mono.describe(description.to<JsonObject>());assert(description["width"]==128&&description["completion"]["kind"]=="transport_complete");
 std::cout<<"Unified immediate/DMA/BUSY/scanout completion, queue policy, leases, revisions, deferred start and failure disposal passed\n";
}
