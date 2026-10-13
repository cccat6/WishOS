#pragma once
#include "Board.h"
#include "Codec.h"
#include <Adafruit_GFX.h>
#include <ArduinoJson.h>
namespace nm {
enum class Kind : uint8_t { Scene = 1, Rle = 2, File = 3 };
struct Payload {
  uint32_t size = 0, delaySec = 0;
  Kind kind = Kind::Scene;
  uint8_t bytes[board::MAX_PAYLOAD];
  // Transient streamed NMP1 storage. It is owned by Runtime and freed after panel.show.
  const uint8_t *external = nullptr;
};
struct Command {
  char op[8] = {};
  int16_t x = 0, y = 0, w = 0, h = 0;
  uint8_t color = 0, scale = 1, fontSize=0;
  char text[97] = {};
};
struct Scene {
  uint8_t background = 1, count = 0;
  Command commands[48];
};
struct FontMetrics {uint8_t w,h,advance;int8_t x,y;};
bool nativeGlyphMetrics(uint32_t,uint8_t,FontMetrics&);
bool nativeGlyphAlpha(uint32_t,uint8_t,uint8_t*,size_t);
bool localTextSupported(const char *,uint8_t);
int nativeTextWidth(const char *,uint8_t);
bool parseScene(const uint8_t *, size_t, Scene &, uint32_t &delaySec);
bool validate(Payload &, Scene &scratch);


class Stripe : public Adafruit_GFX {
public:
  // Largest of a logical row band and a native column band; no full-frame buffer.
  uint8_t bytes[board::HEIGHT * board::STRIPE_ROWS / 2];
  bool columns = false;
  size_t byteCount() const {
    return (columns ? board::HEIGHT : board::WIDTH) * board::STRIPE_ROWS / 2;
  }
  int top = 0;
  Stripe() : Adafruit_GFX(board::WIDTH, board::HEIGHT) {}
  void start(int y, uint8_t c) {
    columns = false;
    top = y;
    memset(bytes, (c << 4) | c, byteCount());
  }
  void startColumns(int x, uint8_t c) {
    columns = true;
    top = x;
    memset(bytes, (c << 4) | c, byteCount());
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (x < 0 || x >= board::WIDTH || y < 0 || y >= board::HEIGHT)
      return;
    size_t at;
    bool lowNibble;
    if (columns) {
      if (x < top || x >= top + board::STRIPE_ROWS) return;
      // Native scan is 720 along the long edge, 480 lines. Rotate the logical canvas CW:
      // native_x = 719 - logical_y; native_y = logical_x.
      const int nativeX = board::HEIGHT - 1 - y;
      at = ((x - top) * board::HEIGHT + nativeX) / 2;
      lowNibble = nativeX & 1;
    } else {
      if (y < top || y >= top + board::STRIPE_ROWS) return;
      at = ((y - top) * board::WIDTH + x) / 2;
      lowNibble = x & 1;
    }
    if (lowNibble)
      bytes[at] = (bytes[at] & 0xf0) | (c & 15);
    else
      bytes[at] = (bytes[at] & 15) | ((c & 15) << 4);
  }
  void fillRect(int16_t x,int16_t y,int16_t w,int16_t h,uint16_t ink) override {
    if(w<=0||h<=0)return;
    // Clip before walking pixels: each native band should paint only its own 16 columns.
    const int left=max(int(x),columns?top:0), right=min(int(x)+w,columns?top+board::STRIPE_ROWS:board::WIDTH);
    const int upper=max(int(y),columns?0:top), lower=min(int(y)+h,columns?board::HEIGHT:top+board::STRIPE_ROWS);
    for(int xx=left;xx<right;++xx)for(int yy=upper;yy<lower;++yy)drawPixel(xx,yy,ink);
  }
  void render(const Scene &);
  void icon(const Command &);
  void text(int x, int y, const char *, uint8_t color, uint8_t scale,uint8_t fontSize=0);
};
} // namespace nm
