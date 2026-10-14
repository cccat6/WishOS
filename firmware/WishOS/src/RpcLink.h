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
private:
 Core&core_;Transport transport_;const char*boot_;char*in_=nullptr,*out_=nullptr;size_t received_=0,total_=0,sending_=0,sent_=0;uint32_t inbound_=0,outbound_=1,generation_=0;bool linked_=false;
 static uint32_t u32(const uint8_t*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
 static void put32(uint8_t*p,uint32_t n){for(int i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
 void reset(){received_=total_=sending_=sent_=0;linked_=false;core_.resources().freeAllocation(in_);core_.resources().freeAllocation(out_);in_=out_=nullptr;}
 bool stage(JsonDocument&d){if(d.overflowed()||measureJson(d)>MAX_WIRE)return false;sending_=serializeJson(d,out_,MAX_WIRE+1);sent_=0;return true;}
 bool receive(const uint8_t*p,size_t n){
  if(n<HeaderBytes||memcmp(p,"WOS1",4))return false;uint32_t id=u32(p+4);size_t offset=p[8]|size_t(p[9])<<8,total=p[10]|size_t(p[11])<<8;
  if(!total||total>MAX_WIRE||offset>total||n-HeaderBytes>total-offset)return false;
  if(!offset){inbound_=id;received_=0;total_=total;}
  if(id!=inbound_||total!=total_||offset!=received_)return false;
  memcpy(in_+received_,p+HeaderBytes,n-HeaderBytes);received_+=n-HeaderBytes;return received_==total_;
 }
public:
 RpcLink(Core&c,Transport t,const char*boot):core_(c),transport_(t),boot_(boot){}
 ~RpcLink(){reset();}
 void disconnect(){reset();}
 bool ready()const{return linked_;}
 void poll(){
  if(!transport_.connected(transport_.context)){if(linked_||in_||out_)reset();return;}
  auto generation=transport_.generation(transport_.context);if(linked_&&generation_!=generation)reset();
  if(!linked_){generation_=generation;in_=static_cast<char*>(core_.resources().allocate(MAX_WIRE+1));out_=static_cast<char*>(core_.resources().allocate(MAX_WIRE+1));if(!in_||!out_){reset();return;}
   linked_=true;JsonDocument hello;hello["type"]="hello";hello["abi"]="wishos-rpc/1";hello["boot_id"]=boot_;hello["message_limit_bytes"]=MAX_WIRE;hello["request_window"]=1;hello["frame_bytes"]=FrameBytes;stage(hello);
  }
  uint8_t frame[FrameBytes];
  // Backpressure preserves the response. Never execute the next call until its
  // predecessor's response has entered the transport in order.
  for(unsigned turn=0;turn<4;++turn){
   if(sending_){size_t n=std::min(PayloadBytes,sending_-sent_);memcpy(frame,"WOS1",4);put32(frame+4,outbound_);frame[8]=sent_;frame[9]=sent_>>8;frame[10]=sending_;frame[11]=sending_>>8;memcpy(frame+HeaderBytes,out_+sent_,n);
    if(!transport_.send(transport_.context,frame,n+HeaderBytes))return;sent_+=n;if(sent_==sending_){sending_=sent_=0;++outbound_;}continue;
   }
   size_t n=sizeof frame;if(!transport_.receive(transport_.context,frame,n))return;
   if(!receive(frame,n))continue;
   in_[total_]=0;JsonDocument request,reply;auto error=deserializeJson(request,in_,total_);received_=total_=0;
   if(error||!request.is<JsonObject>()){reply["ok"]=false;reply["error"]="invalid_json";reply["boot_id"]=boot_;}
   else invokeEnvelope(core_,request.as<JsonObjectConst>(),reply,boot_,core_.now());
   stage(reply);
  }
 }
};
}
