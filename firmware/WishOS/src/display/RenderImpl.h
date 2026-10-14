#include "Content.h"
#include "NativeFont.h"
#include "NativeIcons.h"
namespace nm {
// Bound ArduinoJson allocations, including adversarial nested/large documents.
class BudgetAllocator : public ArduinoJson::Allocator {
  size_t used = 0;
  static constexpr size_t LIMIT = 32768;

public:
  void *allocate(size_t n) override {
    if (n > LIMIT - used)
      return nullptr;
    auto p = (size_t *)malloc(n + sizeof(size_t));
    if (!p)
      return nullptr;
    *p = n;
    used += n;
    return p + 1;
  }
  void deallocate(void *q) override {
    if (!q)
      return;
    auto p = (size_t *)q - 1;
    used -= *p;
    free(p);
  }
  void *reallocate(void *q, size_t n) override {
    if (!q)
      return allocate(n);
    auto p = (size_t *)q - 1;
    size_t old = *p;
    if (n > LIMIT - used + old)
      return nullptr;
    auto v = (size_t *)realloc(p, n + sizeof(size_t));
    if (!v)
      return nullptr;
    *v = n;
    used = used - old + n;
    return v + 1;
  }
};
static const NativeIcon *nativeIcon(const char *name){for(const auto &icon:NATIVE_ICONS)if(!strcmp(name,icon.name))return &icon;return nullptr;}
static bool integer(JsonVariantConst v, int lo, int hi) {
  return v.is<int>() && v.as<int>() >= lo && v.as<int>() <= hi;
}
bool localTextSupported(const char *text,uint8_t size){
 const uint8_t *t=reinterpret_cast<const uint8_t*>(text);
 while(*t){uint32_t cp=*t++;if(cp=='\n')continue;
  if(cp>=0x80){int n=0;uint32_t min=0;
   if(cp>=0xc2&&cp<=0xdf){n=1;min=0x80;cp&=31;}
   else if(cp>=0xe0&&cp<=0xef){n=2;min=0x800;cp&=15;}
   else if(cp>=0xf0&&cp<=0xf4){n=3;min=0x10000;cp&=7;}else return false;
   while(n--){if((*t&0xc0)!=0x80)return false;cp=(cp<<6)|(*t++&63);}
   if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
  }
  bool found=false;for(const auto &g:NATIVE_GLYPHS)if(g.code==cp&&g.size==size){found=true;break;}
  if(!found)return false;
 }
 return true;
}
int nativeTextWidth(const char *text,uint8_t size){
 if(!localTextSupported(text,size))return -1;int width=0;const uint8_t *t=reinterpret_cast<const uint8_t*>(text);
 while(*t){uint32_t cp=*t++;if(cp>=0x80){int n=cp<0xe0?1:cp<0xf0?2:3;cp&=n==1?31:n==2?15:7;while(n--)cp=(cp<<6)|(*t++&63);}
  for(const auto &g:NATIVE_GLYPHS)if(g.code==cp&&g.size==size){width+=g.advance;break;}
 }return width;
}
bool parseScene(const uint8_t *data, size_t len, Scene &s, uint32_t &delaySec) {
  if (!len || len > board::MAX_SCENE)
    return false;
  BudgetAllocator a;
  JsonDocument d(&a);
  if (deserializeJson(d, data, len, DeserializationOption::NestingLimit(6)) || d.overflowed())
    return false;
  if (strcmp(d["format"] | "", "scene-v1") || !d["commands"].is<JsonArray>())
    return false;
  if (!integer(d["background"], 0, 6) || !color(d["background"].as<int>()))
    return false;
  if (!d["next_poll_s"].isNull() && !integer(d["next_poll_s"], 60, 604800))
    return false;
  delaySec = d["next_poll_s"] | 0u;
  s.background = d["background"];
  s.count = 0;
  for (JsonObjectConst o : d["commands"].as<JsonArrayConst>()) {
    if (s.count >= 48)
      return false;
    Command &c = s.commands[s.count++];
    c = Command{};
    const char *op = o["op"] | "";
    if (strcmp(op, "rect") && strcmp(op, "fill") && strcmp(op, "line") && strcmp(op, "circle") &&
        strcmp(op, "text") && strcmp(op,"icon"))
      return false;
    if (!integer(o["x"], 0, 479) || !integer(o["y"], 0, 719) || !integer(o["color"], 0, 6) ||
        !color(o["color"]))
      return false;
    strlcpy(c.op, op, sizeof(c.op));
    c.x = o["x"];
    c.y = o["y"];
    c.color = o["color"];
    if(!strcmp(op,"icon")){
      const char *name=o["name"]|"";if(!nativeIcon(name)||!integer(o["w"],8,96)||!integer(o["h"],8,96)||c.x+o["w"].as<int>()>480||c.y+o["h"].as<int>()>720)return false;
      c.w=o["w"];c.h=o["h"];strlcpy(c.text,name,sizeof(c.text));
    } else if (!strcmp(op, "text")) {
      if (!o["text"].is<const char *>() || strlen(o["text"]) > 96 || (!o["scale"].isNull()&&!integer(o["scale"], 1, 4)))
        return false;
      strlcpy(c.text, o["text"], sizeof(c.text));
      c.scale = o["scale"]|2;
      if(!o["font_size"].isNull()){
        const int size=o["font_size"]|-1;
        if(size!=16&&size!=20&&size!=24&&size!=32&&size!=40&&size!=48&&size!=60)return false;
        c.fontSize=uint8_t(size);
      }
      static const uint8_t sizes[]={0,16,24,40,48};
      if(!localTextSupported(c.text,c.fontSize?c.fontSize:sizes[c.scale]))return false;
    } else {
      if (!integer(o["w"], 0, 720) || !integer(o["h"], 0, 720))
        return false;
      c.w = o["w"];
      c.h = o["h"];
    }
  }
  return true;
}
bool validate(Payload &p, Scene &s) {
  return p.kind==Kind::Scene && p.size<=board::MAX_SCENE && parseScene(p.bytes,p.size,s,p.delaySec);
}
static const NativeGlyph *glyph(uint32_t code,uint8_t size){
  size_t lo=0,hi=sizeof(NATIVE_GLYPHS)/sizeof(NATIVE_GLYPHS[0]);const uint32_t key=(uint32_t(size)<<21)|code;
  while(lo<hi){const size_t mid=(lo+hi)/2;const auto &g=NATIVE_GLYPHS[mid];const uint32_t k=(uint32_t(g.size)<<21)|g.code;if(k<key)lo=mid+1;else hi=mid;}
  if(lo<sizeof(NATIVE_GLYPHS)/sizeof(NATIVE_GLYPHS[0])&&NATIVE_GLYPHS[lo].code==code&&NATIVE_GLYPHS[lo].size==size)return &NATIVE_GLYPHS[lo];
  return code==0x25a1?nullptr:glyph(0x25a1,size);
}
bool nativeGlyphMetrics(uint32_t cp,uint8_t size,FontMetrics&m){auto g=glyph(cp,size);if(!g)return false;m={g->w,g->h,g->advance,g->x,g->y};return true;}
bool nativeGlyphAlpha(uint32_t cp,uint8_t size,uint8_t*out,size_t stride){auto g=glyph(cp,size);if(!g||stride<g->w)return false;uint8_t unpacked[512];const uint8_t*bits=NATIVE_FONT_BITS+g->offset;size_t row=(g->w+7)/8;
 if(g->stored&0x8000){if(row*g->h>sizeof(unpacked)||!lz4Raw(bits,g->stored&0x7fff,unpacked,row*g->h))return false;bits=unpacked;}
 for(unsigned y=0;y<g->h;++y)for(unsigned x=0;x<g->w;++x)out[y*stride+x]=(bits[y*row+x/8]&(0x80>>(x&7)))?255:0;return true;
}
void Stripe::text(int x,int y,const char *t,uint8_t ink,uint8_t scale,uint8_t fontSize){
  static const uint8_t legacySizes[]={0,16,24,40,48};const uint8_t size=fontSize?fontSize:legacySizes[scale];const int startX=x;
  while(*t){uint32_t cp=uint8_t(*t++);
    if(cp=='\n'){x=startX;y+=size+10;continue;}
    if(cp>=0xc2&&cp<=0xf4){int n=cp<0xe0?1:cp<0xf0?2:3;cp&=n==1?31:n==2?15:7;
      while(n--){if((uint8_t(*t)&0xc0)!=0x80){cp='?';break;}cp=(cp<<6)|(uint8_t(*t++)&63);}}
    const auto *g=glyph(cp,size);if(!g)continue;
    const int gx=x+g->x,gy=y+g->y,rowBytes=(g->w+7)/8;
    const int left=max(0,columns?top-gx:-gx),right=min(int(g->w),(columns?top+board::STRIPE_ROWS:board::WIDTH)-gx);
    const int upper=max(0,(columns?0:top)-gy),lower=min(int(g->h),(columns?board::HEIGHT:top+board::STRIPE_ROWS)-gy);
    if(right<=left||lower<=upper){x+=g->advance;continue;}
    uint8_t decoded[512];const uint8_t *bitmap=NATIVE_FONT_BITS+g->offset;
    if(g->stored&0x8000){if(!lz4Raw(bitmap,g->stored&0x7fff,decoded,rowBytes*g->h)){x+=g->advance;continue;}bitmap=decoded;}
    for(int yy=upper;yy<lower;++yy)for(int xx=left;xx<right;++xx)
      if(bitmap[yy*rowBytes+xx/8]&(0x80>>(xx&7)))drawPixel(gx+xx,gy+yy,ink);
    x+=g->advance;if(x>=width())break;
  }
}
void Stripe::icon(const Command &c){
  const auto *asset=nativeIcon(c.text);if(!asset)return;
  const int radius=max(1,(min(int(c.w),int(c.h))+12)/24);
  for(int path=0;path<asset->count;path++){
    const auto &range=asset->paths[path];
    for(int i=1;i<range.count;i++){
      const auto &a=asset->points[range.start+i-1],&b=asset->points[range.start+i];
      int x=c.x+(a.x*c.w+192)/384,y=c.y+(a.y*c.h+192)/384;
      const int endX=c.x+(b.x*c.w+192)/384,endY=c.y+(b.y*c.h+192)/384;
      const int dx=abs(endX-x),sx=x<endX?1:-1,dy=-abs(endY-y),sy=y<endY?1:-1;int error=dx+dy;
      for(;;){fillCircle(x,y,radius,c.color);if(x==endX&&y==endY)break;int twice=2*error;if(twice>=dy){error+=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}}
    }
  }
}
void Stripe::render(const Scene &s) {
  for (int i = 0; i < s.count; i++) {
    const auto &c = s.commands[i];
    if (!strcmp(c.op, "fill"))
      fillRect(c.x, c.y, c.w, c.h, c.color);
    else if (!strcmp(c.op, "rect"))
      drawRect(c.x, c.y, c.w, c.h, c.color);
    else if (!strcmp(c.op, "line"))
      drawLine(c.x, c.y, c.w, c.h, c.color);
    else if (!strcmp(c.op, "circle"))
      drawCircle(c.x, c.y, c.w, c.color);
    else if(!strcmp(c.op,"icon"))icon(c);
    else
      text(c.x, c.y, c.text, c.color, c.scale,c.fontSize);
  }
}
} // namespace nm
