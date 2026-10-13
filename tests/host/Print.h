#pragma once
#include "Arduino.h"
class Print {
public:
  virtual ~Print() {};
  size_t print(const char *s) {
    return write((const uint8_t *)s, strlen(s));
  }
  virtual size_t write(uint8_t) = 0;
  size_t write(const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++)
      write(p[i]);
    return n;
  }
};
