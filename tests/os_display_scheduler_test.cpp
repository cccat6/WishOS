#include "DisplayScheduler.h"
#include "ButtonGestures.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace notesos;
struct Frame:DisplayRequest {unsigned width=0,height=0;};
struct TestAdapter {
 enum Type{Immediate,Dma,Epd,Scanout}type;
 PresentationState&views;std::vector<JsonDocument>receipts;InputPresentation inputPresentation;
 unsigned starts=0,polls=0,leases=0,disposed=0,suspends=0;
 bool defer=false,reject=false,ready=true,done=false,failComplete=false,nativeReadsSource=false,scanoutRunning=false;
 explicit TestAdapter(Type t,PresentationState&v):type(t),views(v){}
 DisplayBegin readiness()const{return failComplete?DisplayBegin::failed("native_transfer_failed"):!ready?DisplayBegin::deferred():DisplayBegin{};}
 DisplayBegin begin(Frame&,uint64_t){++starts;++leases;if(defer)return DisplayBegin::deferred();if(reject)return DisplayBegin::failed("source_unavailable");nativeReadsSource=type==Dma;return {};}
 DisplayOutcome poll(Frame&,uint64_t){
  ++polls;if(type!=Immediate&&!done)return {};
  nativeReadsSource=false;
  if(failComplete){ready=false;return DisplayOutcome::failed("native_transfer_failed");}
  if(type==Scanout){scanoutRunning=true;return DisplayOutcome::success(DisplayEvidence::FrameLatched,"vsync_framebuffer_latch");}
  return DisplayOutcome::success(type==Epd?DisplayEvidence::DeviceReady:DisplayEvidence::TransportComplete,type==Epd?"busy_inactive":"transfer_callback");
 }
 void suspend(Frame&){assert(leases);--leases;++suspends;}
 void completed(Frame&f,const DisplayOutcome&o,uint64_t elapsed){if(f.id==begunId)assert(!nativeReadsSource);if(o.ok())inputPresentation={f.stamp.view,f.stamp.revision,f.stamp.activation};JsonDocument d;o.describe(f,d.to<JsonObject>(),elapsed);assert(measureJson(d)<MAX_EVENT);receipts.push_back(std::move(d));}
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
  assert(!q.enqueue(frame(views,view,1,800,480),Policy::Latest,2).error);assert(adapter.starts==0);q.tick(100);
  if(type==TestAdapter::Immediate){assert(!q.active()&&adapter.receipts.size()==1);assert(adapter.receipts.back()["total_ms"]==0);}
  else{
   assert(q.active()&&adapter.leases==1&&adapter.receipts.empty());
   assert(!views.revise(view,47,0));assert(!q.enqueue(frame(views,view,2),Policy::Latest,2).error);
   assert(!views.revise(view,47,0));assert(!q.enqueue(frame(views,view,3),Policy::Latest,2).error);
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
 adapter.defer=true;assert(!q.enqueue(frame(views,view,1),Policy::Fifo,2).error);q.tick(0);assert(!q.active()&&q.pending()==1&&adapter.leases==0&&adapter.suspends==1&&adapter.receipts.empty());
 adapter.defer=false;adapter.reject=true;q.tick(1);assert(!q.active()&&q.pending()==0&&adapter.leases==0&&adapter.receipts.back()["error"]=="source_unavailable");assert(views.presented().view==0);
 adapter.reject=false;assert(!q.enqueue(frame(views,view,2),Policy::Fifo,2).error);assert(!q.enqueue(frame(views,view,3),Policy::Fifo,2).error);
 auto n=adapter.receipts.size();assert(q.enqueue(frame(views,view,4),Policy::Latest,0).error);assert(q.enqueue(frame(views,view,4),Policy::Fifo,2).error);assert(q.enqueue(frame(views,view,4),Policy::Reject,2).error);assert(q.pending()==2&&adapter.receipts.size()==n);
 assert(q.enqueue(frame(views,view,4),Policy::Fifo,2).kind==DisplayAdmission::Kind::Deferred);assert(q.enqueue(frame(views,view,4),Policy::Reject,2).kind==DisplayAdmission::Kind::Rejected);
 // FIFO can retain accepted revisions, but navigation invalidates queued pages.
 assert(!views.activate(other,false,9));q.tick(2);assert(q.pending()==0&&adapter.starts==2&&adapter.receipts.size()==n+2);
 assert(adapter.receipts.back()["state"]=="superseded"&&adapter.leases==0);
 // A terminal transfer failure releases its source once, fails queued requests without further native
 // work until the adapter is recovered, and preserves the last presented view.
 assert(!q.enqueue(frame(views,other,5),Policy::Fifo,2).error);q.tick(10);assert(q.active()&&adapter.nativeReadsSource);
 assert(!q.enqueue(frame(views,other,6),Policy::Fifo,2).error);adapter.failComplete=true;adapter.done=true;q.tick(20);
 assert(!q.active()&&adapter.leases==0&&q.pending()==1&&views.presented().view==0);auto failed=adapter.receipts.size();
 q.tick(21);assert(adapter.receipts.size()==failed+1&&q.pending()==0&&adapter.receipts.back()["job_id"]==6&&adapter.receipts.back()["error"]=="native_transfer_failed");
 adapter.failComplete=false;adapter.ready=true;assert(!q.enqueue(frame(views,other,60),Policy::Fifo,2).error);q.tick(22);assert(q.pending()==0&&!q.active()&&views.presented().view==other);
 // Queued cancellation releases exactly that request, preserves FIFO order,
 // and never cancels an active DMA/EPD transaction.
 adapter.done=false;assert(!q.enqueue(frame(views,other,7),Policy::Fifo,2).error);q.tick(23);assert(q.active());
 assert(!q.enqueue(frame(views,other,8),Policy::Fifo,2).error);assert(!q.enqueue(frame(views,other,9),Policy::Fifo,2).error);
 assert(!q.cancelQueued(7)&&q.active()&&adapter.nativeReadsSource);assert(q.cancelQueued(8)&&q.pending()==1);
 assert(adapter.receipts.back()["job_id"]==8&&adapter.receipts.back()["state"]=="cancelled");assert(!q.cancelQueued(8));
 adapter.done=true;q.tick(24);q.tick(25);assert(!q.active()&&q.pending()==0&&adapter.receipts.back()["job_id"]==9&&adapter.leases==0);
 // A BUSY display keeps physical input tied to its completed view, not a
 // transfer job ID or a newer desired revision. Debounce preserves that stamp
 // if the next optical frame completes while the key is already down.
 {PresentationState v;uint32_t id;assert(!v.create(8,true,false,id)&&!v.activate(id,false,8));
  Adapter a(TestAdapter::Epd,v);DisplayScheduler<Frame,Adapter>epd(v,a);
  auto first=frame(v,id,401);assert(first.id!=first.stamp.view);assert(!epd.enqueue(first,decltype(epd)::Policy::Latest,2).error);
  epd.tick(0);assert(a.inputPresentation==InputPresentation{});a.done=true;epd.tick(100);
  auto physical=InputPresentation{first.stamp.view,first.stamp.revision,first.stamp.activation};assert(a.inputPresentation==physical);
  std::vector<JsonDocument>input;ButtonGestures keys({"A","B"},&input,[](void*p,const char*,JsonObjectConst data){static_cast<std::vector<JsonDocument>*>(p)->emplace_back(data);});
  bool levels[]={true,true};keys.sampleAll(levels,2,110,a.inputPresentation);
  assert(!v.revise(id,8));a.done=false;auto second=frame(v,id,402);assert(!epd.enqueue(second,decltype(epd)::Policy::Latest,2).error);epd.tick(111);
  assert(a.inputPresentation==physical);a.done=true;epd.tick(120);assert(a.inputPresentation.revision==second.stamp.revision);
  keys.sampleAll(levels,2,140,a.inputPresentation);assert(input.size()==2);
  for(auto&e:input)assert(e["presentation_id"]==physical.view&&e["presentation_revision"]==physical.revision&&e["presentation_activation"]==physical.activation);
  auto prior=a.inputPresentation;a.done=false;assert(!epd.enqueue(frame(v,id,403),decltype(epd)::Policy::Fifo,2).error);epd.tick(150);
  a.failComplete=true;a.done=true;epd.tick(160);assert(a.inputPresentation==prior);
  a.failComplete=false;a.ready=true;Frame clear;clear.id=404;clear.system=true;assert(!epd.enqueue(clear,decltype(epd)::Policy::Fifo,2).error);epd.tick(170);assert(a.inputPresentation==InputPresentation{});
 }
 // A headless runtime can host other resources without constructing a display.
 // Component metadata contains actual geometry/protocol, not an E6 frame contract.
 DisplayComponent mono{"ssd1306","oled","i2c",128,64,DisplayEvidence::TransportComplete,"i2c_transfer","continuous"};JsonDocument description;mono.describe(description.to<JsonObject>());assert(description["width"]==128&&description["completion"]["kind"]=="transport_complete");
 // A resource/initialization deferral never becomes an unbounded wait and
 // never interrupts an already-started transfer. Preparation leases are released.
 {PresentationState v;uint32_t id;assert(!v.create(1,true,false,id)&&!v.activate(id,false,1));Adapter a(TestAdapter::Dma,v);DisplayScheduler<Frame,Adapter>q(v,a);auto f=frame(v,id,90);f.prepareDeadline=200;a.defer=true;assert(!q.enqueue(f,decltype(q)::Policy::Latest,2).error);q.tick(100);assert(!q.active()&&q.pending()==1&&a.leases==0);q.tick(201);assert(!q.active()&&!q.pending()&&a.receipts.back()["error"]=="display_prepare_timeout");}
 std::cout<<"Unified immediate/DMA/BUSY/scanout completion, queue policy, leases, revisions, deferred start and failure disposal passed\n";
}
