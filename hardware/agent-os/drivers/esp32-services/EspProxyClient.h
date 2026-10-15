#pragma once
#include "NetworkProxy.h"
#include <NetworkClient.h>
#include <NetworkClientSecure.h>
#include <Network.h>
#include <lwip/sockets.h>
#include <errno.h>
namespace notesos {
extern NetworkProxy networkProxy;
extern const uint8_t platformCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t platformCaBundleEnd[] asm("_binary_x509_crt_bundle_end");
template<class Client>bool establishProxy(Client&client,const ProxySettings&config,const char*target,uint16_t port){
 std::unique_ptr<ProxyHandshake> handshake(new(std::nothrow)ProxyHandshake());if(!handshake||handshake->begin(config,target,port))return false;
 struct Io{Client&c;int write(const uint8_t*p,size_t n){return c.connected()?int(c.write(p,n)):-1;}int read(uint8_t*p,size_t n){return c.available()?c.read(p,n):c.connected()?0:-1;}}io{client};
 uint32_t began=millis();while(!handshake->ready()&&!handshake->error()&&uint32_t(millis()-began)<config.timeout){handshake->step(io);if(!handshake->ready())delay(1);}return handshake->ready();
}
class ProxyClient:public NetworkClient {
 ProxySettings config_;bool snapshot_=false;
public:
 ProxyClient()=default;explicit ProxyClient(ProxySettings c):config_(c),snapshot_(true){}
 int connect(const char*host,uint16_t port)override{return connect(host,port,10000);}
 int connect(const char*host,uint16_t port,int32_t timeout)override{auto c=snapshot_?config_:networkProxy.snapshot();IPAddress address;if(!Network.hostByName(c.applies(host)?c.host:host,address))return 0;if(!c.applies(host))return NetworkClient::connect(address,port,timeout);if(!NetworkClient::connect(address,c.port,timeout))return 0;if(establishProxy(*this,c,host,port))return 1;stop();return 0;}
 int connect(IPAddress ip,uint16_t port)override{return connect(ip.toString().c_str(),port);}
 int connect(IPAddress ip,uint16_t port,int32_t timeout)override{return connect(ip.toString().c_str(),port,timeout);}
};
class ProxySecureClient:public NetworkClientSecure {
 ProxySettings config_;bool snapshot_=false;
public:
 void usePlatformRoots(){attach_ssl_certificate_bundle(sslclient.get(),true);_use_ca_bundle=true;}
 ProxySecureClient()=default;explicit ProxySecureClient(ProxySettings c):config_(c),snapshot_(true){}
 int connect(const char*host,uint16_t port)override{return connect(host,port,10000);}
 int connect(const char*host,uint16_t port,int32_t timeout)override{auto c=snapshot_?config_:networkProxy.snapshot();_stillinPlainStart=false;if(!c.applies(host)){IPAddress address;if(!Network.hostByName(host,address))return 0;_timeout=timeout;return NetworkClientSecure::connect(address,port,host,_CA_cert,_cert,_private_key);}IPAddress proxy;if(!Network.hostByName(c.host,proxy))return 0;_timeout=timeout;setPlainStart();
 // TCP connects to the proxy, but CA verification and SNI use the origin host.
 if(!NetworkClientSecure::connect(proxy,c.port,host,_CA_cert,_cert,_private_key))return 0;
 // The SDK plain-start connected()/available() path performs a blocking
 // socket peek. Tunnel negotiation instead uses bounded nonblocking raw I/O.
 struct Io{int socket;int write(const uint8_t*p,size_t n){int r=::send(socket,p,n,MSG_DONTWAIT);return r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)?0:r;}int read(uint8_t*p,size_t n){int r=::recv(socket,p,n,MSG_DONTWAIT);return r<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)?0:r==0?-1:r;}}io{fd()};
 std::unique_ptr<ProxyHandshake> handshake(new(std::nothrow)ProxyHandshake());if(!handshake||handshake->begin(c,host,port)){stop();return 0;}uint32_t began=millis();while(!handshake->ready()&&!handshake->error()&&uint32_t(millis()-began)<c.timeout){handshake->step(io);if(!handshake->ready())delay(1);}
 if(handshake->ready()){int result=ssl_starttls_handshake(sslclient.get());sslclient->last_error=result;if(result>=0){_stillinPlainStart=false;return 1;}}stop();_stillinPlainStart=false;return 0;}
 int connect(IPAddress ip,uint16_t port)override{return connect(ip.toString().c_str(),port);}
 int connect(IPAddress ip,uint16_t port,int32_t timeout)override{return connect(ip.toString().c_str(),port,timeout);}
};
}
