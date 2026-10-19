#pragma once
#include <string>
#include <cstdint>
#include <cstddef>
class IPAddress {
 std::string address_="0.0.0.0";
public:
 IPAddress()=default;explicit IPAddress(const char*address):address_(address){}
 std::string toString()const{return address_;}
};
class NetworkClient {
public:
 unsigned dialCalls=0;std::string dialAddress;uint16_t dialPort=0;int dialResult=0;
 virtual ~NetworkClient()=default;
 virtual int connect(const char*,uint16_t){return 0;}
 virtual int connect(const char*,uint16_t,int32_t){return 0;}
 virtual int connect(IPAddress ip,uint16_t port){return connect(ip,port,10000);}
 virtual int connect(IPAddress ip,uint16_t port,int32_t){++dialCalls;dialAddress=ip.toString();dialPort=port;return dialResult;}
 int connected(){return 0;}int available(){return 0;}int read(uint8_t*,size_t){return -1;}size_t write(const uint8_t*,size_t){return 0;}void stop(){}
};
