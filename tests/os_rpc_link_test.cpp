#include "RpcLink.h"
#include <cassert>
#include <deque>
#include <vector>
#include <iostream>
using namespace notesos;
struct Wire {bool connected=true,blocked=false;uint32_t generation=1;std::deque<std::vector<uint8_t>>in,out;
 void request(const char*s,uint32_t id=1){size_t n=strlen(s);for(size_t at=0;at<n;at+=1012){size_t count=std::min(size_t(1012),n-at);std::vector<uint8_t>frame(12+count);memcpy(frame.data(),"WOS1",4);for(int i=0;i<4;++i)frame[4+i]=id>>(8*i);frame[8]=at;frame[9]=at>>8;frame[10]=n;frame[11]=n>>8;memcpy(frame.data()+12,s+at,count);in.push_back(frame);}}
 std::string reply(){std::string s;while(!out.empty()){auto frame=out.front();out.pop_front();s.append((char*)frame.data()+12,frame.size()-12);size_t n=frame[10]|size_t(frame[11])<<8;if(s.size()==n)break;}return s;}
};
static unsigned calls=0;
int main(){Core core;Wire wire;auto baseline=core.resources().promised();core.add({"/action","write","",[](const char*,JsonObjectConst a,JsonObject o)->const char*{++calls;o["echo"]=a;return nullptr;}});
 {RpcLink link(core,{&wire,[](void*p){return ((Wire*)p)->connected;},[](void*p){return ((Wire*)p)->generation;},[](void*p,const uint8_t*b,size_t n){auto&w=*(Wire*)p;if(w.blocked)return false;w.out.emplace_back(b,b+n);return true;},[](void*p,uint8_t*b,size_t&n){auto&w=*(Wire*)p;if(w.in.empty())return false;auto frame=w.in.front();w.in.pop_front();assert(frame.size()<=n);n=frame.size();memcpy(b,frame.data(),n);return true;}},"boot");
 link.poll();assert(wire.reply().find("hello")!=std::string::npos);auto idleMemory=core.resources().promised();
 wire.request(R"({"id":1,"path":"/action","method":"write","args":{"value":42}})");wire.blocked=true;link.poll();assert(calls==1&&core.resources().promised()==idleMemory);wire.request(R"({"id":2,"path":"/action","method":"write"})",2);link.poll();assert(calls==1);
 wire.blocked=false;link.poll(false);assert(calls==1&&link.outboundDrained()&&wire.in.size()==1);assert(wire.reply().find("42")!=std::string::npos);link.poll();link.poll();assert(calls==2);wire.reply();
 std::string big(2800,'x');std::string request="{\"id\":3,\"path\":\"/action\",\"method\":\"write\",\"args\":{\"data\":\""+big+"\"}}";wire.request(request.c_str(),3);link.poll();assert(link.outboundDrained());assert(calls==3&&wire.reply().find(big)!=std::string::npos);assert(core.resources().promised()==idleMemory);
 // Saturating the application allocation allowance must not strand the RPC
 // control path: its connected workspace was already admitted.
 JsonDocument limit;limit["memory_limit_bytes"]=uint32_t(core.resources().promised());assert(!core.resources().configure(limit.as<JsonObjectConst>()));assert(!core.resources().allocate(1));wire.request(R"({"id":5,"path":"/action","method":"write","args":{"value":9}})",5);for(int i=0;i<3;++i)link.poll();assert(calls==4&&wire.reply().find("9")!=std::string::npos);limit["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(limit.as<JsonObjectConst>()));
 wire.connected=false;link.poll();assert(!link.ready()&&core.resources().promised()==baseline);wire.connected=true;++wire.generation;link.poll();assert(wire.reply().find("hello")!=std::string::npos);
 wire.request(R"({"id":4,"path":"/action","method":"write","expected_boot_id":"old"})",4);link.poll();link.poll();assert(calls==4&&wire.reply().find("boot_changed")!=std::string::npos);
 }assert(core.resources().promised()==baseline);std::cout<<"RPC fragmentation, backpressure, event-loop dispatch, reconnect cleanup and boot guard passed\n";
}
