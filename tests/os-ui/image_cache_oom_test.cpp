#include "UiMemory.h"
#include <lvgl.h>
#include <lvgl_private.h>
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <vector>
using namespace notesos;
static size_t calls=0,failAt=SIZE_MAX;static unsigned infoCalls=0;
static void* resize(void*p,size_t n){if(!n){free(p);return nullptr;}if(calls++==failAt)return nullptr;return realloc(p,n);}
static lv_image_header_t header(){lv_image_header_t h{};h.magic=LV_IMAGE_HEADER_MAGIC;h.cf=LV_COLOR_FORMAT_RGB565;h.w=3;h.h=2;h.stride=6;return h;}
int main(){
 ResourceBudget memory;memory.heap(ResourceBudget::Region::Internal,{nullptr,nullptr,nullptr,resize});assert(bindUiMemory(memory));lv_init();lv_image_header_cache_resize(8,true);lv_image_cache_resize(8192,true);
 lv_fs_drv_t fs;lv_fs_drv_init(&fs);fs.letter='Z';fs.open_cb=[](lv_fs_drv_t*,const char*,lv_fs_mode_t)->void*{return reinterpret_cast<void*>(1);};fs.close_cb=[](lv_fs_drv_t*,void*){return LV_FS_RES_OK;};fs.seek_cb=[](lv_fs_drv_t*,void*,uint32_t,lv_fs_whence_t){return LV_FS_RES_OK;};lv_fs_drv_register(&fs);
 auto decoder=lv_image_decoder_create();assert(decoder);lv_image_decoder_set_info_cb(decoder,[](lv_image_decoder_t*,lv_image_decoder_dsc_t*,lv_image_header_t*out){++infoCalls;*out=header();return LV_RESULT_OK;});lv_image_decoder_set_open_cb(decoder,[](lv_image_decoder_t*,lv_image_decoder_dsc_t*){return LV_RESULT_OK;});
 // Retain another valid key so a failed filename allocation would enter the
 // real cache's strcmp callback. Cache OOM keeps already-read image metadata.
 for(size_t failure=0;failure<5;++failure){
  lv_image_cache_drop(nullptr);failAt=SIZE_MAX;lv_image_header_t h{};assert(lv_image_decoder_get_info("Z:/first",&h)==LV_RESULT_OK);auto baseline=uiMemoryUsed();
  calls=0;failAt=failure;assert(lv_image_decoder_get_info("Z:/second",&h)==LV_RESULT_OK);assert(h.w==3&&h.h==2&&h.stride==6);failAt=SIZE_MAX;
  assert(lv_image_decoder_get_info("Z:/second",&h)==LV_RESULT_OK);auto before=infoCalls;assert(lv_image_decoder_get_info("Z:/second",&h)==LV_RESULT_OK&&infoCalls==before);
  lv_image_header_cache_drop("Z:/second");assert(uiMemoryUsed()==baseline);
 }
 // The decoded-image cache also owns a copied file key. It duplicates before
 // insertion, so strdup or node OOM leaves the caller's pixel buffer untouched.
 auto add=[&](const char*source,lv_draw_buf_t*buf){lv_image_cache_data_t key{};key.src_type=LV_IMAGE_SRC_FILE;key.src=source;key.slot.size=buf->data_size;return lv_image_decoder_add_to_cache(decoder,&key,buf,nullptr);};
 auto release=[](lv_cache_entry_t*entry){lv_cache_release(const_cast<lv_cache_t*>(lv_cache_entry_get_cache(entry)),entry,nullptr);};
 for(size_t failure=0;failure<5;++failure){
  failAt=SIZE_MAX;lv_image_cache_drop(nullptr);auto first=lv_draw_buf_create(3,2,LV_COLOR_FORMAT_RGB565,LV_STRIDE_AUTO);assert(first);auto entry=add("Z:/first",first);assert(entry);release(entry);auto baseline=uiMemoryUsed();
  auto buf=lv_draw_buf_create(3,2,LV_COLOR_FORMAT_RGB565,LV_STRIDE_AUTO);assert(buf);calls=0;failAt=failure;entry=add("Z:/second",buf);failAt=SIZE_MAX;
  if(entry){release(entry);lv_image_cache_drop("Z:/second");}else lv_draw_buf_destroy(buf);
  assert(uiMemoryUsed()==baseline);
  buf=lv_draw_buf_create(3,2,LV_COLOR_FORMAT_RGB565,LV_STRIDE_AUTO);assert(buf);entry=add("Z:/second",buf);assert(entry);release(entry);lv_image_cache_drop("Z:/second");assert(uiMemoryUsed()==baseline);
 }
 lv_image_cache_drop(nullptr);lv_image_decoder_delete(decoder);lv_image_header_cache_drop(nullptr);
 // A valid decoded PNG larger than the optional image cache still renders.
 // Repeat open/close to prove that the transient decoded buffer is released.
 std::ifstream input("hardware/agent-os/runtime/lckfb-suite/assets/brand/boot-logo.png",std::ios::binary);assert(input);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)),{});
 lv_image_dsc_t image{};image.header.magic=LV_IMAGE_HEADER_MAGIC;image.header.cf=LV_COLOR_FORMAT_RAW_ALPHA;image.header.w=160;image.header.h=160;image.data=bytes.data();image.data_size=bytes.size();
 for(size_t cacheBytes:{size_t(0),size_t(1024),size_t(65536),size_t(262144)}){
  lv_image_cache_drop(nullptr);lv_image_cache_resize(cacheBytes,false);lv_cache_reserve(LV_GLOBAL_DEFAULT()->img_cache,0,nullptr);
  size_t baseline=0;for(unsigned i=0;i<12;++i){lv_image_decoder_dsc_t opened{};assert(lv_image_decoder_open(&opened,&image,nullptr)==LV_RESULT_OK);assert(opened.decoded&&opened.decoded->data_size>=160*160*4);if(i==0)assert(opened.header.w==160&&opened.header.h==160);if(cacheBytes&&cacheBytes<opened.decoded->data_size)assert(opened.args.no_cache&&opened.cache_entry==nullptr);lv_image_decoder_close(&opened);if(i==0)baseline=uiMemoryUsed();else assert(uiMemoryUsed()==baseline);}
 }
 lv_image_cache_drop(nullptr);lv_deinit();assert(uiMemoryUsed()==0&&memory.promised()==0);
 std::cout<<"LVGL header/decoded file-cache strdup and cache-node OOM preserve valid headers, keys and caller ownership\n";
}
