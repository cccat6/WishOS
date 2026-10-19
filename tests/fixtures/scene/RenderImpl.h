#include "Content.h"
#include "display/NativeText.h"
#include "NativeIcons.h"
namespace nm {
static const NativeIcon *nativeIcon(const char *name){for(const auto &icon:NATIVE_ICONS)if(!strcmp(name,icon.name))return &icon;return nullptr;}
static bool integer(JsonVariantConst v, int lo, int hi) {
  return v.is<int>() && v.as<int>() >= lo && v.as<int>() <= hi;
}
bool parseScene(JsonObjectConst d, Scene &s) {
  for(auto field:d)if(strcmp(field.key().c_str(),"format")&&strcmp(field.key().c_str(),"background")&&strcmp(field.key().c_str(),"commands"))return false;
  if (strcmp(d["format"] | "", "scene-v1") || !d["commands"].is<JsonArrayConst>())
    return false;
  if (!integer(d["background"], 0, 6) || !color(d["background"].as<int>()))
    return false;
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
void Stripe::text(int x,int y,const char *t,uint8_t ink,uint8_t scale,uint8_t fontSize){
  static const uint8_t legacySizes[]={0,16,24,40,48};const uint8_t size=fontSize?fontSize:legacySizes[scale];const int startX=x;
  while(*t){uint32_t cp=uint8_t(*t++);
    if(cp=='\n'){x=startX;y+=size+10;continue;}
    if(cp>=0xc2&&cp<=0xf4){int n=cp<0xe0?1:cp<0xf0?2:3;cp&=n==1?31:n==2?15:7;
      while(n--){if((uint8_t(*t)&0xc0)!=0x80){cp='?';break;}cp=(cp<<6)|(uint8_t(*t++)&63);}}
    FontMetrics g;if(!nativeGlyphMetrics(cp,size,g))continue;
    const int gx=x+g.x,gy=y+g.y,rowBytes=(g.w+7)/8;
    const int left=max(0,columns?top-gx:-gx),right=min(int(g.w),(columns?top+board::STRIPE_ROWS:board::WIDTH)-gx);
    const int upper=max(0,(columns?0:top)-gy),lower=min(int(g.h),(columns?board::HEIGHT:top+board::STRIPE_ROWS)-gy);
    if(right<=left||lower<=upper){x+=g.advance;continue;}
    uint8_t bitmap[NATIVE_GLYPH_BYTES];if(!nativeGlyphBitmap(cp,size,bitmap,sizeof bitmap,g)){x+=g.advance;continue;}
    for(int yy=upper;yy<lower;++yy)for(int xx=left;xx<right;++xx)
      if(bitmap[yy*rowBytes+xx/8]&(0x80>>(xx&7)))drawPixel(gx+xx,gy+yy,ink);
    x+=g.advance;if(x>=width())break;
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
