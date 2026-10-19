// UI-service implementation: filesystem-backed complete fonts, with faces
// created only for requested pixel sizes. VFS leases protect live font streams.
// The existing face-cache limit bounds inactive faces across all families;
// inactive families release their leases and revalidate before reuse.
struct UiVectorFace {unsigned size,cacheGlyphs;lv_font_t*font;UiVectorFace*next;uint64_t used,created;};
struct UiVectorFamily {Vfs::Access lease;FileProvider*provider=nullptr;uint64_t revision=0,prepared=0;bool stale=false;char path[256]{};UiVectorFace*faces=nullptr;UiVectorFamily*next=nullptr;explicit UiVectorFamily(Vfs&v):lease(v,false){}};
static UiVectorFamily*uiVectorFamilies=nullptr;static uint64_t uiFontPreparation=0,uiFontUse=0;
static unsigned fontBe(const uint8_t*p,unsigned n){unsigned v=0;while(n--)v=(v<<8)|*p++;return v;}
static const char*validateVectorFont(const char*path){const char*relative;auto provider=vfs.resolve(path,relative);if(!provider)return "font_path";
 // Native face admission uses the same pinned-cmap contract as discovery. The
 // bounded read window belongs to the system heap, not the event-loop stack.
 auto memory=core.resources().allocate(sizeof(TrueTypeCoverage));if(!memory)return "ui_memory_budget";
 auto reader=new(memory)TrueTypeCoverage(*provider,relative);auto error=reader->open();reader->~TrueTypeCoverage();core.resources().freeAllocation(reader);return error;
}
static void freeVectorFace(UiVectorFace*f){lv_tiny_ttf_destroy(f->font);core.resources().freeAllocation(f);}
static void reapVectorFonts(bool all=false,int unusedLimit=-1){
 unsigned limit=unusedLimit<0?uiFontPolicy.cacheFaces:unsigned(unusedLimit),unused=0;
 for(auto family=uiVectorFamilies;family;family=family->next){const char*relative;auto provider=vfs.resolve(family->path,relative);family->stale|=provider!=family->provider||!provider||!provider->available()||!family->revision||provider->contentRevision()!=family->revision;}
 for(auto family=uiVectorFamilies;family;family=family->next)for(auto face=&family->faces;*face;){auto entry=*face;bool active=uiUsesFont(uiRoot,entry->font);
  if(all||(!active&&family->stale)){*face=entry->next;freeVectorFace(entry);}else{if(!active)++unused;face=&entry->next;}
 }
 while(unused>limit){UiVectorFace**oldest=nullptr;
  for(auto family=uiVectorFamilies;family;family=family->next)for(auto face=&family->faces;*face;face=&(*face)->next)if(!uiUsesFont(uiRoot,(*face)->font)&&(!oldest||(*face)->used<(*oldest)->used))oldest=face;
  auto removed=*oldest;*oldest=removed->next;freeVectorFace(removed);--unused;
 }
 for(auto family=&uiVectorFamilies;*family;){auto f=*family;bool active=false;for(auto face=f->faces;face;face=face->next)active|=uiUsesFont(uiRoot,face->font);
  if(!active)f->lease.clear();
  if(!f->faces){*family=f->next;f->~UiVectorFamily();core.resources().freeAllocation(f);}else family=&f->next;
 }
}
static void discardVectorPreparation(){
 for(auto family=uiVectorFamilies;family;family=family->next)for(auto face=&family->faces;*face;){auto entry=*face;if(entry->created==uiFontPreparation&&!uiUsesFont(uiRoot,entry->font)){*face=entry->next;freeVectorFace(entry);}else face=&entry->next;}
 reapVectorFonts(false,uiMemoryFailed()?0:int(uiFontPolicy.cacheFaces));
}
static const char*checkPreparedVectorFonts(){for(auto family=uiVectorFamilies;family;family=family->next)if(family->prepared==uiFontPreparation){const char*relative;auto provider=vfs.resolve(family->path,relative);if(provider!=family->provider||!provider||!provider->available()||provider->contentRevision()!=family->revision)return "font_source_changed";}return nullptr;}
static bool uiUsesVectorFile(const char*path){for(auto f=uiVectorFamilies;f;f=f->next)if(!strcmp(f->path,path))for(auto g=f->faces;g;g=g->next)if(uiUsesFont(uiRoot,g->font))return true;return false;}
static const lv_font_t*vectorFont(const char*path,unsigned size){if(size<8||size>96){uiError="font_size_range";return nullptr;}char full[256];if(!normalizePath(uiBase,path,full,sizeof full)){uiError="font_path";return nullptr;}
 const char*relative;auto provider=vfs.resolve(full,relative);if(!provider){uiError="font_path";return nullptr;}if(!provider->available()){uiError="provider_unavailable";return nullptr;}auto revision=provider->contentRevision();
 UiVectorFamily*family=nullptr;
 for(auto entry=uiVectorFamilies;entry;entry=entry->next)if(!strcmp(entry->path,full)){
  if(entry->provider!=provider||entry->revision!=revision)entry->stale=true;
  if(entry->prepared==uiFontPreparation){if(entry->provider!=provider||entry->revision!=revision){uiError="font_source_changed";return nullptr;}family=entry;break;}
  if(!entry->stale&&revision&&entry->provider==provider&&entry->revision==revision&&!family)family=entry;
 }
 if(!family){auto memory=core.resources().allocate(sizeof(UiVectorFamily));if(!memory){uiError="ui_memory_budget";return nullptr;}family=new(memory)UiVectorFamily(vfs);family->provider=provider;family->revision=revision;copy(family->path,sizeof family->path,full);
  const char*error=family->lease.add(full,provider->caseInsensitive());if(!error)error=validateVectorFont(full);if(!error&&(!provider->available()||revision!=provider->contentRevision()))error="font_source_changed";
  if(error){family->~UiVectorFamily();core.resources().freeAllocation(family);uiError=error;return nullptr;}family->next=uiVectorFamilies;uiVectorFamilies=family;
 }else if(auto error=family->lease.add(full,provider->caseInsensitive())){uiError=error;return nullptr;}
 if(!provider->available()||revision!=provider->contentRevision()){uiError="font_source_changed";return nullptr;}family->prepared=uiFontPreparation;
 unsigned cacheGlyphs=uiFontPolicy.cacheGlyphs;
 for(auto link=&family->faces;*link;link=&(*link)->next)if((*link)->size==size&&(*link)->cacheGlyphs==cacheGlyphs){auto face=*link;*link=face->next;face->next=family->faces;family->faces=face;face->used=++uiFontUse;return face->font;}
 char qualified[260];snprintf(qualified,sizeof qualified,"V:%s",full);auto font=lv_tiny_ttf_create_file_ex(qualified,size,LV_FONT_KERNING_NONE,cacheGlyphs);if(!font){uiError=uiReadError?uiReadError:"font_load_failed";return nullptr;}if(!provider->available()||revision!=provider->contentRevision()){lv_tiny_ttf_destroy(font);uiError="font_source_changed";return nullptr;}auto memory=core.resources().allocate(sizeof(UiVectorFace));if(!memory){lv_tiny_ttf_destroy(font);uiError="ui_memory_budget";return nullptr;}auto f=new(memory)UiVectorFace{size,cacheGlyphs,font,family->faces,++uiFontUse,uiFontPreparation};family->faces=f;return font;
}
