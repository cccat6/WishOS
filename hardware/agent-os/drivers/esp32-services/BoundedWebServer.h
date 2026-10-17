#pragma once
#include "HttpIngress.h"
#include "ResourceBudget.h"
#include <WebServer.h>
#include <sys/socket.h>
namespace notesos {
// Keep the pinned SDK's routing/response implementation. Only its blocking
// receive boundary is gated, without consuming bytes or dispatching app work.
class BoundedWebServer:public WebServer {
 ResourceBudget&memory_;size_t bodyLimit_,headerLimit_;uint32_t deadlineMs_;char*header_=nullptr;
 void releaseHeader(){memory_.freeAllocation(header_);header_=nullptr;}
 void reject(unsigned status){char response[160];int size=snprintf(response,sizeof response,"HTTP/1.1 %u Request rejected\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",status);::send(_currentClient.fd(),response,size,MSG_DONTWAIT);_currentClient.stop();releaseHeader();WebServer::handleClient();}
public:
 BoundedWebServer(uint16_t port,ResourceBudget&memory,size_t bodyLimit,size_t headerLimit=1536,uint32_t deadlineMs=5000):WebServer(port),memory_(memory),bodyLimit_(bodyLimit),headerLimit_(headerLimit),deadlineMs_(deadlineMs){}
 ~BoundedWebServer(){releaseHeader();}
 void describe(JsonObject o)const{auto p=o["request_admission"].to<JsonObject>();p["body_limit_bytes"]=bodyLimit_;p["header_limit_bytes"]=headerLimit_;p["deadline_ms"]=deadlineMs_;p["framing"]="Content-Length";p["body_representation"]="text; binary file data uses resource chunk APIs";p["incomplete_request_wait"]="nonblocking";}
 void handleClient()override{
  if(_currentStatus==HC_NONE){releaseHeader();_currentClient=_server.accept();if(!_currentClient)return;_currentStatus=HC_WAIT_READ;_statusChange=millis();}
  if(_currentStatus==HC_WAIT_READ&&_currentClient.connected()){
   if(uint32_t(millis()-_statusChange)>=deadlineMs_){reject(408);return;}
   size_t available=_currentClient.available();if(!available)return;
   if(!header_)header_=static_cast<char*>(memory_.allocate(headerLimit_));if(!header_){reject(503);return;}
   auto count=::recv(_currentClient.fd(),header_,headerLimit_,MSG_PEEK|MSG_DONTWAIT);if(count<=0)return;
   auto result=HttpIngress::inspect(header_,count,available,headerLimit_,bodyLimit_);
   if(result.state==HttpIngress::State::Rejected){reject(result.status);return;}
   if(result.state==HttpIngress::State::Waiting)return;
   releaseHeader();
  }
  WebServer::handleClient();if(_currentStatus==HC_NONE)releaseHeader();
 }
};
}
