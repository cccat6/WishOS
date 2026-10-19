#include "drivers/esp32-services/EspProxyClient.h"
#include <cassert>
#include <iostream>
namespace notesos {NetworkProxy networkProxy;}
using namespace notesos;
int main(){
 // Arduino 3.3.11 returns 1 for success and nonzero EAI errors for failure.
 // Neither direct nor proxy connections may dial an unresolved zero address.
 for(bool proxy:{false,true})for(int error:{0,-3,202,203}){
  ProxySettings settings;if(proxy){strcpy(settings.mode,"http_connect");strcpy(settings.host,"proxy.example");settings.bypass[0]=0;}
  Network.result=error;Network.calls=0;ProxyClient plain(settings);ProxySecureClient secure(settings);
  assert(plain.connect("api.example",443,250)==0&&plain.dialCalls==0);
  assert(secure.connect("api.example",443,250)==0&&secure.dialCalls==0);
  assert(Network.calls==2&&Network.hostname==(proxy?"proxy.example":"api.example"));
 }
 Network.result=1;ProxySettings direct;ProxyClient plain(direct);ProxySecureClient secure(direct);plain.dialResult=secure.dialResult=1;
 assert(plain.connect("api.example",80,250)==1&&plain.dialCalls==1&&plain.dialAddress=="192.0.2.10"&&plain.dialPort==80);
 assert(secure.connect("api.example",443,250)==1&&secure.dialCalls==1&&secure.dialAddress=="192.0.2.10"&&secure.origin=="api.example");
 // A successful proxy-host lookup proceeds to that proxy. Target DNS/SNI
 // remain origin-specific; a TCP refusal leaves tunnel negotiation untouched.
 ProxySettings proxy;strcpy(proxy.mode,"http_connect");strcpy(proxy.host,"proxy.example");proxy.bypass[0]=0;proxy.port=8080;
 ProxyClient viaProxy(proxy);ProxySecureClient secureProxy(proxy);
 assert(!viaProxy.connect("api.example",443,250)&&viaProxy.dialCalls==1&&viaProxy.dialPort==8080);
 assert(!secureProxy.connect("api.example",443,250)&&secureProxy.dialCalls==1&&secureProxy.dialPort==8080&&secureProxy.origin=="api.example");
 std::cout<<"ESP proxy adapters reject all resolver errors before TCP/TLS dialing; direct/proxy success preserves target routing and SNI\n";
}
