#include "UiMemory.h"
#include <lvgl.h>
namespace {
notesos::ResourceBudget*budget=nullptr;size_t used=0,peak=0,count=0;bool failed=false;
}
namespace notesos {
bool bindUiMemory(ResourceBudget&m){if(used&&budget!=&m)return false;budget=&m;return true;}
void clearUiMemoryFailure(){failed=false;}
bool uiMemoryFailed(){return failed;}
size_t uiMemoryUsed(){return used;}
}
extern "C" {
void lv_mem_init(void){}
void lv_mem_deinit(void){}
lv_mem_pool_t lv_mem_add_pool(void*,size_t){return nullptr;}
void lv_mem_remove_pool(lv_mem_pool_t){}
void*lv_malloc_core(size_t n){if(!budget){failed=true;return nullptr;}auto p=budget->allocate(n);if(!p){failed=true;return nullptr;}used+=n;++count;if(used>peak)peak=used;return p;}
void*lv_realloc_core(void*p,size_t n){if(!p)return lv_malloc_core(n);if(!n){lv_free_core(p);return nullptr;}size_t old=budget->allocationSize(p);auto next=budget->reallocate(p,n);if(!next){failed=true;return nullptr;}used=used-old+budget->allocationSize(next);if(used>peak)peak=used;return next;}
void lv_free_core(void*p){if(!p)return;used-=budget->allocationSize(p);--count;budget->freeAllocation(p);}
void lv_mem_monitor_core(lv_mem_monitor_t*m){*m={};m->used_cnt=count;m->max_used=peak;}
lv_result_t lv_mem_test_core(void){return budget?LV_RESULT_OK:LV_RESULT_INVALID;}
}
