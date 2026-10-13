#include "RemoteRequests.h"
#include "HttpApplications.h"
#include "SocketService.h"
#include "DatagramService.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>
using namespace notesos;
static JsonDocument json(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static bool transitioning=false;static int invoked=0;
static const char*gate(const char*,const char*m){return transitioning&&strcmp(m,"read")?"power_transition":nullptr;}
static const char*operation(const char*,JsonObjectConst,JsonObject){++invoked;return nullptr;}
static unsigned availablePort(){int fd=socket(AF_INET,SOCK_STREAM,0);sockaddr_in a{};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!bind(fd,(sockaddr*)&a,sizeof a));socklen_t n=sizeof a;getsockname(fd,(sockaddr*)&a,&n);close(fd);return ntohs(a.sin_port);}
int main(){Core c;JsonDocument out;
 c.add({"/test","read,write","",operation});c.setInvocationGate(gate);transitioning=true;JsonDocument empty;
 assert(!strcmp(c.invoke("/test","write",empty.as<JsonObjectConst>(),out.to<JsonObject>()),"power_transition")&&invoked==0);
 assert(!c.invoke("/test","read",empty.as<JsonObjectConst>(),out.to<JsonObject>())&&invoked==1);transitioning=false;
 assert(!c.invoke("/test","write",empty.as<JsonObjectConst>(),out.to<JsonObject>())&&invoked==2);
 RemoteRequests q(c);auto a=json(R"({"request_id":"fixed","route":"agent","payload":{"text":"hello"}})");
 assert(!q.invoke("submit",a.as<JsonObjectConst>(),out.to<JsonObject>(),0,"boot"));auto id=out["id"].as<unsigned>();assert(id);
 assert(!q.invoke("submit",a.as<JsonObjectConst>(),out.to<JsonObject>(),1,"boot")&&out["id"]==id);
 a["payload"]["text"]="changed";assert(q.invoke("submit",a.as<JsonObjectConst>(),out.to<JsonObject>(),2,"boot"));
 a=json(R"({"id":1,"ok":true,"result":{"value":4}})");assert(!q.invoke("complete",a.as<JsonObjectConst>(),out.to<JsonObject>(),3,"boot"));assert(!q.pending());
 assert(!q.invoke("complete",a.as<JsonObjectConst>(),out.to<JsonObject>(),4,"boot"));a["result"]["value"]=5;assert(q.invoke("complete",a.as<JsonObjectConst>(),out.to<JsonObject>(),5,"boot"));
 RemoteRequests fifo(c);auto f=json(R"({"route":"service","payload":{}})");
 assert(!fifo.invoke("submit",f.as<JsonObjectConst>(),out.to<JsonObject>(),0,"b"));assert(!fifo.invoke("submit",f.as<JsonObjectConst>(),out.to<JsonObject>(),0,"b"));
 auto done=json(R"({"id":1,"ok":true,"result":null})");assert(!fifo.invoke("complete",done.as<JsonObjectConst>(),out.to<JsonObject>(),1,"b"));assert(!fifo.invoke("release",done.as<JsonObjectConst>(),out.to<JsonObject>(),1,"b"));
 assert(!fifo.invoke("submit",f.as<JsonObjectConst>(),out.to<JsonObject>(),2,"b"));assert(!fifo.invoke("peek",empty.as<JsonObjectConst>(),out.to<JsonObject>(),3,"b")&&out["id"]==2);
 HttpApplications http(c);a=json(R"({"path":"/app/hello","body":"hello","content_type":"text/plain"})");assert(!http.invoke("route",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(http.handle("/app/hello","GET","",0,out.to<JsonObject>())&&out["body"]=="hello");
 a["dynamic"]=true;assert(!http.invoke("route",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(http.handle("/app/hello","GET","data",0,out.to<JsonObject>())&&out["status"]==202);
 a=json(R"({"id":1,"body":"reply","status":200})");assert(!http.invoke("respond",a.as<JsonObjectConst>(),out.to<JsonObject>())&&out["done"]==true&&out["body"]=="reply");
 a=json(R"({"path":"/","body":"User runtime home","content_type":"text/html"})");assert(!http.invoke("route",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(http.handle("/","GET","",0,out.to<JsonObject>())&&out["body"]=="User runtime home");
 a["dynamic"]="yes";assert(http.invoke("route",a.as<JsonObjectConst>(),out.to<JsonObject>()));assert(http.handle("/","GET","",0,out.to<JsonObject>())&&out["body"]=="User runtime home");
 a.remove("dynamic");a["path"]="/os/invoke";assert(http.invoke("route",a.as<JsonObjectConst>(),out.to<JsonObject>()));
 SocketService tcp(c);a.clear();a["address"]="127.0.0.1";a["port"]=availablePort();unsigned port=a["port"];assert(!tcp.invoke("listen",a.as<JsonObjectConst>(),out.to<JsonObject>()));
 int peer=socket(AF_INET,SOCK_STREAM,0);sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);assert(!connect(peer,(sockaddr*)&addr,sizeof addr));a.clear();for(int retry=0;retry<200;++retry){tcp.poll();out.clear();assert(!tcp.invoke("read",a.as<JsonObjectConst>(),out.to<JsonObject>()));if(out["sockets"].size()==2)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}auto sockets=out["sockets"].as<JsonArrayConst>();assert(sockets.size()==2);unsigned connected=sockets[1]["id"];
 uint8_t bytes[]={0,1,255};assert(send(peer,bytes,3,0)==3);tcp.poll();a["id"]=connected;for(int retry=0;retry<200;++retry){out.clear();assert(!tcp.invoke("read",a.as<JsonObjectConst>(),out.to<JsonObject>()));if(out["bytes"]==3)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}assert(out["hex"]=="0001ff");a["hex"]="aabb";assert(!tcp.invoke("write",a.as<JsonObjectConst>(),out.to<JsonObject>())&&out["written"]==2);assert(recv(peer,bytes,3,0)==2&&bytes[0]==0xaa&&bytes[1]==0xbb);close(peer);tcp.poll();tcp.closeAll();assert(!tcp.active());
 std::cout<<"Remote idempotency, dynamic HTTP routes and real TCP binary I/O passed\n";
}
