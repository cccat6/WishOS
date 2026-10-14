#pragma once
#include <math.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#define PROGMEM
using std::max;
using std::min;
class __FlashStringHelper;
class String : public std::string {
public:
  using std::string::string;
  String(const std::string &s):std::string(s){}
  bool startsWith(const char *p)const{return rfind(p,0)==0;}
  bool isEmpty()const{return empty();}
  int indexOf(char c,int start=0)const{auto n=find(c,start);return n==npos?-1:int(n);}
  String substring(size_t a,size_t b=npos)const{return substr(a,b==npos?npos:b-a);}

};
inline void yield() {}
inline float radians(float degrees) {
  return degrees * 0.017453292519943295f;
}

#ifndef OUTPUT_OPEN_DRAIN
#define OUTPUT_OPEN_DRAIN 0x13
#endif
