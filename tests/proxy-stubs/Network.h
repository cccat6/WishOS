#pragma once
#include "NetworkClient.h"
struct Resolver {
 int result=1;unsigned calls=0;std::string hostname;
 int hostByName(const char*host,IPAddress&address){++calls;hostname=host;address=result==1?IPAddress("192.0.2.10"):IPAddress();return result;}
};
inline Resolver Network;
inline uint32_t millis(){return 0;}inline void delay(unsigned){}
