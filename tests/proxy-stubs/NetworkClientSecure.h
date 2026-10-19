#pragma once
#include "NetworkClient.h"
#include <memory>
struct SslContext {int last_error=0;};
inline void attach_ssl_certificate_bundle(SslContext*,bool){}
inline int ssl_starttls_handshake(SslContext*){return -1;}
class NetworkClientSecure:public NetworkClient {
public:
 std::unique_ptr<SslContext>sslclient{new SslContext};bool _stillinPlainStart=false,_use_ca_bundle=false;int32_t _timeout=0;
 const char*_CA_cert=nullptr,*_cert=nullptr,*_private_key=nullptr;std::string origin;
 int connect(IPAddress ip,uint16_t port,const char*host,const char*,const char*,const char*){origin=host;return NetworkClient::connect(ip,port,_timeout);}
 void setPlainStart(){}int fd(){return -1;}
};
