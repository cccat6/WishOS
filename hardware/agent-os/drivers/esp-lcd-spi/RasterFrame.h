#pragma once
#include <stdint.h>
#include <string.h>
namespace notesos::drivers {
// Copy the renderer's contiguous RGB565 rectangle into the panel's native
// frame. Geometry/rotation have already been validated by presentation setup.
class LcdRasterFrame {
 uint16_t*frame_;int width_,height_;unsigned rotation_;
public:
 LcdRasterFrame(uint16_t*frame,unsigned width,unsigned height,unsigned rotation):frame_(frame),width_(width),height_(height),rotation_(rotation){}
 void write(int x,int y,unsigned width,unsigned height,const uint16_t*pixels){
  if(!rotation_){for(unsigned row=0;row<height;++row)memcpy(frame_+(y+row)*width_+x,pixels+row*width,width*sizeof(uint16_t));return;}
  for(unsigned row=0;row<height;++row){
   int index,step;
   switch(rotation_){
    case 90:index=x*width_+width_-1-y-int(row);step=width_;break;
    case 180:index=(height_-1-y-int(row))*width_+width_-1-x;step=-1;break;
    default:index=(height_-1-x)*width_+y+int(row);step=-width_;break; // 270
   }
   for(unsigned column=0;column<width;++column,index+=step)frame_[index]=pixels[row*width+column];
  }
 }
};
}
