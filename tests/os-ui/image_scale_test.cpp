#include "UiMemory.h"
#include <lvgl.h>
#include <lvgl_private.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace notesos;

static void* resize(void* pointer,size_t bytes) {
    if(!bytes) { free(pointer); return nullptr; }
    return realloc(pointer,bytes);
}

int main() {
    ResourceBudget memory;
    memory.heap(ResourceBudget::Region::Internal,{nullptr,nullptr,nullptr,resize});
    assert(bindUiMemory(memory));
    lv_init();
    unsigned cases=0;
    for(auto format:{LV_COLOR_FORMAT_RGB565,LV_COLOR_FORMAT_RGB565_SWAPPED,
                     LV_COLOR_FORMAT_RGB565A8,LV_COLOR_FORMAT_RGB888,
                     LV_COLOR_FORMAT_XRGB8888,LV_COLOR_FORMAT_ARGB8888,
                     LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED,LV_COLOR_FORMAT_A8,
                     LV_COLOR_FORMAT_L8}) {
        const int sourceWidth=9,sourceHeight=7;
        unsigned sourceBytes=lv_color_format_get_size(format);
        if(format==LV_COLOR_FORMAT_RGB565A8) sourceBytes=2;
        const int stride=sourceWidth*sourceBytes;
        std::vector<uint8_t> source(stride*sourceHeight+
            (format==LV_COLOR_FORMAT_RGB565A8?sourceWidth*sourceHeight:0));
        for(size_t i=0;i<source.size();++i) source[i]=(i*37+i/3*11)%251;
        unsigned colorBytes=sourceBytes;
        const bool separateAlpha=format==LV_COLOR_FORMAT_RGB565 ||
            format==LV_COLOR_FORMAT_RGB565_SWAPPED || format==LV_COLOR_FORMAT_RGB565A8;
        if(format==LV_COLOR_FORMAT_RGB888) colorBytes=4;
        if(format==LV_COLOR_FORMAT_L8) colorBytes=2;
        for(int zoom:{160,300,1024,2560}) for(int pivot:{0,4}) {
            lv_draw_image_dsc_t descriptor;
            lv_draw_image_dsc_init(&descriptor);
            descriptor.scale_x=zoom; descriptor.scale_y=zoom+137;
            descriptor.pivot={pivot,3}; descriptor.antialias=true;
            const lv_area_t area{-3,-2,93,78};
            const int width=lv_area_get_width(&area),height=lv_area_get_height(&area);
            const size_t colorSize=width*height*colorBytes;
            std::vector<uint8_t> whole(colorSize+(separateAlpha?width*height:0));
            auto partitioned=whole;
            lv_draw_sw_transform(&area,source.data(),sourceWidth,sourceHeight,stride,
                                 &descriptor,nullptr,format,whole.data());
            assert(std::any_of(whole.begin(),whole.end(),[](uint8_t value){return value;}));
            // Both clipping axes vary. Edge tiles have different dimensions;
            // logical scale, source format and pivot remain identical.
            for(int y=area.y1;y<=area.y2;y+=8) for(int x=area.x1;x<=area.x2;x+=13) {
                lv_area_t tile{x,y,std::min(x+12,area.x2),std::min(y+7,area.y2)};
                const int tileWidth=lv_area_get_width(&tile),tileHeight=lv_area_get_height(&tile);
                std::vector<uint8_t> chunk(tileWidth*tileHeight*(colorBytes+(separateAlpha?1:0)));
                lv_draw_sw_transform(&tile,source.data(),sourceWidth,sourceHeight,stride,
                                     &descriptor,nullptr,format,chunk.data());
                for(int row=0;row<tileHeight;++row) {
                    const size_t destination=(y-area.y1+row)*width+x-area.x1;
                    memcpy(partitioned.data()+destination*colorBytes,
                           chunk.data()+row*tileWidth*colorBytes,tileWidth*colorBytes);
                    if(separateAlpha) memcpy(partitioned.data()+colorSize+destination,
                        chunk.data()+tileWidth*tileHeight*colorBytes+row*tileWidth,tileWidth);
                }
            }
            if(whole!=partitioned) {
                size_t differences=0;
                for(size_t i=0;i<whole.size();++i) differences+=whole[i]!=partitioned[i];
                std::cerr<<"format="<<format<<" zoom="<<zoom<<" pivot="<<pivot
                         <<" differences="<<differences<<"\n";
            }
            assert(whole==partitioned);
            ++cases;
        }
    }
    lv_deinit();
    assert(memory.promised()==0);
    std::cout<<cases<<" scale/pivot/color-format whole-vs-clipped XY tile byte-exact cases passed\n";
}
