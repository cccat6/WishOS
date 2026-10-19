// CPU raster storage is independent of any native DMA staging buffer. It uses
// the system's normal Auto heap and changes only with a successful preparation.
static constexpr uint32_t uiDefaultDrawBufferBytes=boardpack::DISPLAY_WIDTH*8*2;
static uint8_t*uiDrawBuffer=nullptr;static uint32_t uiDrawBufferBytes=0;
static uint32_t desiredUiDrawBufferBytes(){return policies.get<uint32_t>("/sys/ui","draw_buffer_bytes",uiDefaultDrawBufferBytes);}
static void bindUiDrawBuffer(uint8_t*buffer,uint32_t bytes){lv_display_set_buffers(uiDisplay,buffer,nullptr,bytes,LV_DISPLAY_RENDER_MODE_PARTIAL);uiDrawBuffer=buffer;uiDrawBufferBytes=bytes;}
class UiDrawBufferCandidate {
 uint8_t*buffer_=nullptr;uint32_t bytes_=0;
public:
 bool prepare(){bytes_=desiredUiDrawBufferBytes();auto width=std::max(boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT);if(bytes_<lv_draw_buf_width_to_stride(width,LV_COLOR_FORMAT_RGB565)){uiError="ui_draw_buffer_too_small";return false;}if(bytes_==uiDrawBufferBytes)return true;buffer_=static_cast<uint8_t*>(core.resources().allocate(bytes_));if(!buffer_){uiError="ui_memory_budget";return false;}return true;}
 void commit(){if(buffer_){auto previous=uiDrawBuffer;bindUiDrawBuffer(buffer_,bytes_);buffer_=nullptr;core.resources().freeAllocation(previous);}}
 ~UiDrawBufferCandidate(){core.resources().freeAllocation(buffer_);}
};
