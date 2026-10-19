#include "drivers/esp-lcd-spi/RasterFrame.h"
#include <vector>
#include <algorithm>
#include <utility>
#include <cassert>
#include <iostream>
using notesos::drivers::LcdRasterFrame;
// An independent canvas rotation serves as the reference; the driver copies
// arbitrary flush rectangles instead of rotating a complete canvas this way.
static std::vector<uint16_t> reference(std::vector<uint16_t> canvas,unsigned w,unsigned h,unsigned turns){
 for(unsigned turn=0;turn<turns;++turn){std::vector<uint16_t> next(canvas.size());for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x)next[x*h+h-1-y]=canvas[y*w+x];canvas.swap(next);std::swap(w,h);}return canvas;
}
static void scenario(unsigned panelWidth,unsigned panelHeight,unsigned rotation){
 unsigned w=rotation%180?panelHeight:panelWidth,h=rotation%180?panelWidth:panelHeight;std::vector<uint16_t> source(w*h),actual(w*h+2,0),working(w*h,0);
 uint32_t seed=17;for(auto&v:source){seed=seed*1664525u+1013904223u;v=seed>>16;}actual.front()=0x1234;actual.back()=0x5678;
 LcdRasterFrame raster(actual.data()+1,panelWidth,panelHeight,rotation);
 auto compare=[&](){auto expected=reference(working,w,h,rotation/90);assert(actual.front()==0x1234&&actual.back()==0x5678);for(size_t i=0;i<expected.size();++i)assert(actual[i+1]==expected[i]);};
 auto copy=[&](unsigned x,unsigned y,unsigned width,unsigned height){std::vector<uint16_t> block(width*height);for(unsigned row=0;row<height;++row)for(unsigned col=0;col<width;++col){auto value=source[(y+row)*w+x+col];block[row*width+col]=value;working[(y+row)*w+x+col]=value;}raster.write(x,y,width,height,block.data());compare();};
 copy(0,0,w,h); // Every corner and interior pixel follows the same orientation.
 std::fill(actual.begin()+1,actual.end()-1,0);std::fill(working.begin(),working.end(),0);
 for(unsigned i=0;i<20;++i){seed=seed*1664525u+1013904223u;unsigned x=seed%w;seed=seed*1664525u+1013904223u;unsigned y=seed%h;seed=seed*1664525u+1013904223u;unsigned width=1+seed%(w-x);seed=seed*1664525u+1013904223u;unsigned height=1+seed%(h-y);copy(x,y,width,height);}
 std::fill(actual.begin()+1,actual.end()-1,0);std::fill(working.begin(),working.end(),0);
 struct Region{unsigned x,y,w,h;};std::vector<Region> regions;unsigned dx=w/3+1,dy=h/7+1;for(unsigned y=0;y<h;y+=dy)for(unsigned x=0;x<w;x+=dx)regions.push_back({x,y,std::min(dx,w-x),std::min(dy,h-y)});
 // Out-of-order partition delivery exercises physical stride and untouched areas.
 for(size_t n=regions.size();n;--n){auto r=regions[n-1];copy(r.x,r.y,r.w,r.h);}assert(working==source);
}
int main(){for(auto shape:{std::pair<unsigned,unsigned>{1,1},{3,5},{7,9},{320,240},{240,240}})for(unsigned rotation:{0u,90u,180u,270u})scenario(shape.first,shape.second,rotation);std::cout<<"LCD RGB565 row copy: four rotations, arbitrary partial flushes, odd geometry and partition equivalence passed\n";}
