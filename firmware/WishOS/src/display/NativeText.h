#pragma once
#include <stdint.h>
#include <stddef.h>
namespace nm {
struct FontMetrics {uint8_t w,h,advance;int8_t x,y;};
constexpr size_t NATIVE_GLYPH_BYTES=512;
bool nativeGlyphMetrics(uint32_t,uint8_t,FontMetrics&);
bool nativeGlyphBitmap(uint32_t,uint8_t,uint8_t*,size_t,FontMetrics&);
bool nativeGlyphAlpha(uint32_t,uint8_t,uint8_t*,size_t);
bool localTextSupported(const char*,uint8_t);
int nativeTextWidth(const char*,uint8_t);
}
