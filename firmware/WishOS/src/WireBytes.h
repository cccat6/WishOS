#pragma once
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
namespace notesos {
inline void hex(const uint8_t *p, size_t n, char *out) {
  static const char a[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = a[p[i] >> 4];
    out[2 * i + 1] = a[p[i] & 15];
  }
  out[2 * n] = 0;
}
inline bool unhex(const char *s, uint8_t *out, size_t cap, size_t &n) {
  if (!s || strlen(s) % 2 || strlen(s) / 2 > cap)
    return false;
  n = strlen(s) / 2;
  auto nib = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  for (size_t i = 0; i < n; i++) {
    int a = nib(s[2 * i]), b = nib(s[2 * i + 1]);
    if (a < 0 || b < 0)
      return false;
    out[i] = (a << 4) | b;
  }
  return true;
}

inline const char*wirePayload(JsonObjectConst a,uint8_t*out,size_t capacity,size_t&n){
 if(a.containsKey("text")==a.containsKey("hex"))return "one_payload_required";
 const char*key=a.containsKey("hex")?"hex":"text";if(!a[key].is<const char*>())return "payload_type";auto value=a[key].as<JsonString>();
 if(!strcmp(key,"hex")){if(value.size()!=strlen(value.c_str())||!unhex(value.c_str(),out,capacity,n))return "invalid_hex";}
 else{n=value.size();if(n>capacity)return "chunk_limit";memcpy(out,value.c_str(),n);}return nullptr;
}

}
