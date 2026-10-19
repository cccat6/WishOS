// Positive source facts share the configured image-cache budget. File access
// and current policy admission are checked by the caller for every use.
struct UiImageMetadata {FileProvider*provider;uint64_t revision;FileInfo file;lv_image_header_t header;bool png;char path[256];UiImageMetadata*next;};
static UiImageMetadata*uiImageMetadata=nullptr;
static size_t uiImageMetadataBytes=0;static unsigned uiImageMetadataCount=0;
static uint32_t uiAppliedDecodedImageCache=UINT32_MAX;
static void syncUiDecodedImageCache(){
 uint32_t budget=uiAppliedImageCache==UINT32_MAX?0:uiAppliedImageCache;
 uint32_t decoded=budget-uint32_t(uiImageMetadataBytes);
 if(uiDisplay&&decoded!=uiAppliedDecodedImageCache){
  // Reserve no new bytes: trim only residents exceeding the new maximum.
  // LVGL's resize(..., true) instead reserves an entire empty cache.
  lv_image_cache_resize(decoded,false);lv_cache_reserve(LV_GLOBAL_DEFAULT()->img_cache,0,nullptr);uiAppliedDecodedImageCache=decoded;
 }
}
static void removeUiImageMetadata(UiImageMetadata**link){auto entry=*link;*link=entry->next;uiImageMetadataBytes-=sizeof(UiImageMetadata);--uiImageMetadataCount;core.resources().freeAllocation(entry);}
static void evictUiImageMetadata(){auto link=&uiImageMetadata;while((*link)->next)link=&(*link)->next;removeUiImageMetadata(link);}
static void clearUiImageMetadata(){while(uiImageMetadata)removeUiImageMetadata(&uiImageMetadata);syncUiDecodedImageCache();}
static void configureUiImageCache(uint32_t budget){uiAppliedImageCache=budget;while(uiImageMetadataBytes>budget)evictUiImageMetadata();syncUiDecodedImageCache();}
static void invalidateUiImageMetadata(FileProvider&provider,uint64_t revision){
 for(auto link=&uiImageMetadata;*link;)if((*link)->provider==&provider&&(!revision||(*link)->revision!=revision))removeUiImageMetadata(link);else link=&(*link)->next;
 syncUiDecodedImageCache();
}
static const UiImageMetadata*findUiImageMetadata(FileProvider&provider,const char*path,uint64_t revision,const char*qualified){
 // LVGL keys its decoder caches by path. A different provider resolving that
 // same path must retire the preceding provider's header and decoded pixels.
 for(auto link=&uiImageMetadata;*link;)if((*link)->provider!=&provider&&!strcmp((*link)->path,path)){lv_image_cache_drop(qualified);removeUiImageMetadata(link);}else link=&(*link)->next;
 syncUiDecodedImageCache();
 if(!revision||!uiAppliedImageCache||uiAppliedImageCache==UINT32_MAX)return nullptr;
 for(auto link=&uiImageMetadata;*link;link=&(*link)->next)if((*link)->provider==&provider&&(*link)->revision==revision&&!strcmp((*link)->path,path)){
  auto found=*link;*link=found->next;found->next=uiImageMetadata;uiImageMetadata=found;return found;
 }return nullptr;
}
static void rememberUiImageMetadata(FileProvider&provider,const char*path,uint64_t revision,const FileInfo&file,const lv_image_header_t&header,bool png){
 if(!revision||uiAppliedImageCache==UINT32_MAX||sizeof(UiImageMetadata)>uiAppliedImageCache||!uiAppliedHeaderCache||uiAppliedHeaderCache==UINT32_MAX)return;
 while(uiImageMetadata&&(uiImageMetadataBytes+sizeof(UiImageMetadata)>uiAppliedImageCache||uiImageMetadataCount>=uiAppliedHeaderCache))evictUiImageMetadata();
 auto entry=static_cast<UiImageMetadata*>(core.resources().allocate(sizeof(UiImageMetadata)));if(!entry){syncUiDecodedImageCache();return;}
 *entry={&provider,revision,file,header,png,{},uiImageMetadata};copy(entry->path,sizeof entry->path,path);uiImageMetadata=entry;uiImageMetadataBytes+=sizeof(UiImageMetadata);++uiImageMetadataCount;syncUiDecodedImageCache();
}
