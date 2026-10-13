#pragma once
#include "PresentationState.h"
#include <atomic>
namespace notesos {
// Compiled adapter contract. Request extends DisplayRequest with its source and
// profile data. All methods run in the service loop, never from an ISR:
// begin/poll do bounded work and return; they never wait through a refresh.
// available(), begin(Request&, now), poll(Request&, now), suspend(Request&),
// completed(Request&, outcome, elapsed), dispose(Request&).
// begin() may defer only before hardware work starts; suspend releases temporary
// preparation leases. Terminal poll results require source buffers to be released
// by DMA/native I/O. A faulted adapter may remain unavailable until recovery.
struct DisplayRequest {
 uint32_t id=0,owner=0;PresentationState::Stamp stamp;
 bool latestOnly=true,system=false;
};
enum class DisplayEvidence:uint8_t {None,TransportComplete,DeviceReady,FrameLatched,Cached};
inline const char*displayEvidenceName(DisplayEvidence value){
 switch(value){case DisplayEvidence::TransportComplete:return "transport_complete";case DisplayEvidence::DeviceReady:return "device_ready";case DisplayEvidence::FrameLatched:return "frame_latched";case DisplayEvidence::Cached:return "unchanged";default:return "none";}
}
struct DisplayOutcome {
 enum class Kind:uint8_t {Pending,Success,Failure,Superseded,Cancelled};
 Kind kind=Kind::Pending;DisplayEvidence evidence=DisplayEvidence::None;
 const char*source="none",*error="none";
 bool terminal()const{return kind!=Kind::Pending;}
 bool ok()const{return kind==Kind::Success;}
 const char*state()const{return kind==Kind::Pending?"running":kind==Kind::Success?(evidence==DisplayEvidence::Cached?"unchanged":"presented"):kind==Kind::Superseded?"superseded":kind==Kind::Cancelled?"cancelled":"failed";}
 static DisplayOutcome success(DisplayEvidence evidence,const char*source){return {Kind::Success,evidence,source,"none"};}
 static DisplayOutcome unchanged(){return success(DisplayEvidence::Cached,"previous_content");}
 static DisplayOutcome failed(const char*error){return {Kind::Failure,DisplayEvidence::None,"none",error};}
 static DisplayOutcome superseded(const char*reason){return {Kind::Superseded,DisplayEvidence::None,"none",reason};}
 static DisplayOutcome cancelled(){return {Kind::Cancelled,DisplayEvidence::None,"none","cancelled"};}
 void describe(const DisplayRequest&request,JsonObject out,uint64_t elapsed)const{
  out["job_id"]=request.id;out["owner"]=request.owner;out["view_id"]=request.stamp.view;
  out["view_revision"]=request.stamp.revision;out["activation"]=request.stamp.activation;
  out["state"]=state();out["success"]=ok();out["error"]=error;out["total_ms"]=elapsed;
  out["completion"]["kind"]=displayEvidenceName(evidence);out["completion"]["source"]=source;
 }
};
struct DisplayBegin {
 enum class Kind:uint8_t {Started,Deferred,Failed};Kind kind=Kind::Started;const char*error="none";
 static DisplayBegin deferred(){return {Kind::Deferred,"none"};}
 static DisplayBegin failed(const char*e){return {Kind::Failed,e};}
};
struct DisplayComponent {
 const char*id;const char*technology;const char*transport;unsigned width,height;
 DisplayEvidence evidence;const char*completionSource;const char*refreshModel;
 void describe(JsonObject out)const{
  out["id"]=id;out["interface"]="display/1";out["technology"]=technology;out["transport"]=transport;
  out["width"]=width;out["height"]=height;out["installation"]="compiled_native";
  out["completion"]["kind"]=displayEvidenceName(evidence);out["completion"]["source"]=completionSource;
  out["refresh_model"]=refreshModel;out["profile_selection"]="per_presentation";
 }
};
// The queue owns accepted requests. Drivers own pixel encoding, transfer phases,
// completion detection and safe cancellation points. No fixed frame size, palette,
// BUSY pin, refresh delay or transport exists in this scheduler.
template<class Request,class Adapter,unsigned Capacity=4>class DisplayScheduler {
public:
 enum class Policy:uint8_t {Latest,Fifo,Reject};
private:
 PresentationState&views_;Adapter&adapter_;Request pending_[Capacity]{},current_{};
 unsigned count_=0;std::atomic<bool>active_{false};uint64_t began_=0;
 void discardFront(const DisplayOutcome&outcome){
  adapter_.completed(pending_[0],outcome,0);adapter_.dispose(pending_[0]);
  for(unsigned i=1;i<count_;++i)pending_[i-1]=pending_[i];pending_[--count_]={};
 }
 void finish(const DisplayOutcome&outcome,uint64_t now){
  if(outcome.ok()&&current_.stamp.view)views_.completed(current_.stamp,displayEvidenceName(outcome.evidence));
  adapter_.completed(current_,outcome,now-began_);adapter_.dispose(current_);current_={};active_=false;
 }
public:
 DisplayScheduler(PresentationState&views,Adapter&adapter):views_(views),adapter_(adapter){}
 bool active()const{return active_.load();}unsigned pending()const{return count_;}
 static constexpr unsigned capacity(){return Capacity;}
 Request&current(){return current_;}const Request&current()const{return current_;}
 bool relevant(const DisplayRequest&job)const{
  if(!job.stamp.view)return job.system;auto now=views_.stamp(job.stamp.view);
  return now.activation&&now.activation==job.stamp.activation&&(!job.latestOnly||now.revision==job.stamp.revision);
 }
 const char*enqueue(const Request&job,Policy policy,unsigned limit){
  if(!limit||limit>Capacity)return "display_queue_limit";
  if(policy==Policy::Reject&&(active_||count_))return "display_busy";
  if(policy!=Policy::Latest&&count_>=limit)return "display_queue_full";
  if(policy==Policy::Latest)while(count_)discardFront(DisplayOutcome::superseded("newer_presentation"));
  pending_[count_]=job;pending_[count_].latestOnly=policy==Policy::Latest;++count_;return nullptr;
 }
 void tick(uint64_t now){
  while(count_&&!relevant(pending_[0]))discardFront(DisplayOutcome::superseded("view_changed"));
  if(!active_&&count_&&adapter_.available()){
   current_=pending_[0];auto start=adapter_.begin(current_,now);
   if(start.kind==DisplayBegin::Kind::Deferred){adapter_.suspend(current_);current_={};return;}
   for(unsigned i=1;i<count_;++i)pending_[i-1]=pending_[i];pending_[--count_]={};active_=true;began_=now;
   if(start.kind==DisplayBegin::Kind::Failed){finish(DisplayOutcome::failed(start.error),now);return;}
  }
  if(active_){auto result=adapter_.poll(current_,now);if(result.terminal())finish(result,now);}
 }
 // A transport entrypoint can finish a stream only after its adapter has safely
 // cancelled native I/O. Active hardware is never interrupted by queue policy.
 void finishCancelled(uint64_t now){finish(DisplayOutcome::cancelled(),now);}
 void finishFailed(const char*error,uint64_t now){finish(DisplayOutcome::failed(error),now);}
};
}
