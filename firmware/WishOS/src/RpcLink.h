#pragma once
#include "RpcEnvelope.h"
namespace notesos {
// Ordered control channel over bounded transport messages. It owns no application
// workflow. The native adapter chooses connection policy and transport; calls are
// dispatched only by the system event loop, including runtime replacement.
class RpcLink {
public:
 struct Transport{void*context;bool(*connected)(void*);uint32_t(*generation)(void*);bool(*send)(void*,const uint8_t*,size_t);bool(*receive)(void*,uint8_t*,size_t&);};
 static constexpr size_t FrameBytes=1024,HeaderBytes=12,PayloadBytes=FrameBytes-HeaderBytes;
 static constexpr size_t FramesPerMessage=(MAX_WIRE+PayloadBytes-1)/PayloadBytes;
private:
 Core&core_;Transport transport_;const char*boot_;char*message_=nullptr;uint8_t*frame_=nullptr;size_t received_=0,total_=0,sending_=0,sent_=0;uint32_t inbound_=0,outbound_=1,generation_=0;bool linked_=false;uint64_t lastActivity_=0;
 static uint32_t u32(const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
 static void put32(uint8_t*p,uint32_t n){for(int i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 void reset(){received_=total_=sending_=sent_=0;linked_=false;core_.resources().freeAllocation(message_);core_.resources().freeAllocation(frame_);frame_=nullptr;message_=nullptr;}
 bool workspace(){if(!message_)message_=static_cast<char*>(core_.resources().allocate(MAX_WIRE+1));return message_!=nullptr;}
 bool stage(JsonDocument&d){if(d.overflowed()||measureJson(d)>MAX_WIRE||!workspace())return false;sending_=serializeJson(d,message_,MAX_WIRE+1);sent_=0;return true;}
 bool receive(const uint8_t*p,size_t n){
  if(n<HeaderBytes||memcmp(p,"WOS1",4))return false;uint32_t id=u32(p+4);size_t offset=p[8]|size_t(p[9])<<8,total=p[10]|size_t(p[11])<<8;
  if(!total||total>MAX_WIRE||offset>total||n-HeaderBytes>total-offset)return false;
  if(!offset){inbound_=id;received_=0;total_=total;}
  if(id!=inbound_||total!=total_||offset!=received_)return false;
  if(!workspace())return false;memcpy(message_+received_,p+HeaderBytes,n-HeaderBytes);received_+=n-HeaderBytes;return received_==total_;
 }
public:
 RpcLink(Core&c,Transport t,const char*boot):core_(c),transport_(t),boot_(boot){}
 ~RpcLink(){reset();}
 void disconnect(){reset();}
 bool ready()const{return linked_;}
 bool outboundDrained()const{return !sending_;}
 bool quiescent(uint64_t now,uint32_t quietMs)const{return linked_&&!sending_&&!received_&&now-lastActivity_>=quietMs;}
 void poll(bool acceptRequests=true){
  if(!transport_.connected(transport_.context)){if(linked_||message_)reset();return;}
  auto generation=transport_.generation(transport_.context);if(linked_&&generation_!=generation)reset();
  if(!linked_){generation_=generation;frame_=static_cast<uint8_t*>(core_.resources().allocate(FrameBytes));if(!frame_||!workspace()){reset();return;}
   linked_=true;JsonDocument hello;hello["type"]="hello";hello["abi"]="wishos-rpc/1";hello["boot_id"]=boot_;hello["message_limit_bytes"]=MAX_WIRE;hello["request_window"]=1;hello["frame_bytes"]=FrameBytes;if(!stage(hello)){reset();return;}
  }
  auto frame=frame_;
  // Backpressure preserves the response. Never execute the next call until its
  // predecessor's response has entered the transport in order.
  // One bounded request/response burst must not yield between fragments just
  // because a UI render may be scheduled before the next system-loop turn.
  for(size_t turn=0;turn<2*FramesPerMessage+1;++turn){
   if(sending_){size_t n=std::min(PayloadBytes,sending_-sent_);memcpy(frame,"WOS1",4);put32(frame+4,outbound_);frame[8]=sent_;frame[9]=sent_>>8;frame[10]=sending_;frame[11]=sending_>>8;memcpy(frame+HeaderBytes,message_+sent_,n);
    if(!transport_.send(transport_.context,frame,n+HeaderBytes))return;lastActivity_=core_.now();sent_+=n;if(sent_==sending_){sending_=sent_=0;++outbound_;}continue;
   }
   if(!acceptRequests)return;size_t n=FrameBytes;if(!transport_.receive(transport_.context,frame,n))return;lastActivity_=core_.now();
   if(!receive(frame,n))continue;
   // The one-message window lets receive and reply share storage. Const input
   // makes JSON strings owned by the document before the reply overwrites it.
   // Keep this shared workspace while connected: an app reaching its memory
   // ceiling must not prevent control requests from being received.
   message_[total_]=0;JsonDocument request,reply;auto error=deserializeJson(request,static_cast<const char*>(message_),total_);received_=total_=0;
   if(error||!request.is<JsonObject>()){reply["ok"]=false;reply["error"]="invalid_json";reply["boot_id"]=boot_;}
   else invokeEnvelope(core_,request.as<JsonObjectConst>(),reply,boot_,core_.now());
   stage(reply);
  }
 }
};
}
