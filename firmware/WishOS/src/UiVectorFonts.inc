// UI-service implementation: filesystem-backed complete fonts, with faces
// created only for requested pixel sizes. VFS leases protect live font streams.
struct UiVectorFace {unsigned size,cacheGlyphs;lv_font_t*font;UiVectorFace*next;};
struct UiVectorFamily {Vfs::Access lease;char path[256]{};UiVectorFace*faces=nullptr;UiVectorFamily*next=nullptr;explicit UiVectorFamily(Vfs&v):lease(v,false){}};
static UiVectorFamily*uiVectorFamilies=nullptr;
static unsigned fontBe(const uint8_t*p,unsigned n){unsigned v=0;while(n--)v=(v<<8)|*p++;return v;}
static const char*validateVectorFont(const char*path){const char*relative;auto provider=vfs.resolve(path,relative);if(!provider)return "font_path";FileInfo info;if(auto e=provider->stat(relative,info))return e;if(info.directory||info.size<12)return "font_format";
 uint8_t head[12];size_t got=0;if(auto e=provider->read(relative,0,head,12,got))return e;if(got!=12||fontBe(head,4)!=0x10000)return "font_requires_truetype_outlines";
 unsigned tables=fontBe(head+4,2);if(!tables||12+uint64_t(tables)*16>info.size)return "font_table_range";unsigned required=0;
 for(unsigned i=0;i<tables;++i){uint8_t entry[16];if(auto e=provider->read(relative,12+i*16,entry,16,got))return e;if(got!=16)return "font_short_read";uint64_t offset=fontBe(entry+8,4),bytes=fontBe(entry+12,4);if(offset>info.size||bytes>info.size-offset)return "font_table_range";const char*tags[]={"head","hhea","hmtx","maxp","cmap","loca","glyf"};for(unsigned n=0;n<7;++n)if(!memcmp(entry,tags[n],4))required|=1u<<n;}
 return required==127?nullptr:"font_required_table";
}
static void freeVectorFace(UiVectorFace*f){lv_tiny_ttf_destroy(f->font);core.resources().freeAllocation(f);}
static void reapVectorFonts(bool all=false){auto family=&uiVectorFamilies;while(*family){auto&f=**family;auto face=&f.faces;while(*face){auto entry=*face;if(all||!uiUsesFont(uiRoot,entry->font)){*face=entry->next;freeVectorFace(entry);}else face=&entry->next;}if(!f.faces){auto removed=*family;*family=f.next;removed->~UiVectorFamily();core.resources().freeAllocation(removed);}else family=&f.next;}}
static bool uiUsesVectorFile(const char*path){for(auto f=uiVectorFamilies;f;f=f->next)if(!strcmp(f->path,path))for(auto g=f->faces;g;g=g->next)if(uiUsesFont(uiRoot,g->font))return true;return false;}
static const lv_font_t*vectorFont(const char*path,unsigned size){if(size<8||size>96){uiError="font_size_range";return nullptr;}char full[256];if(!normalizePath(uiBase,path,full,sizeof full)){uiError="font_path";return nullptr;}
 auto family=uiVectorFamilies;while(family&&strcmp(family->path,full))family=family->next;
 if(!family){auto memory=core.resources().allocate(sizeof(UiVectorFamily));if(!memory){uiError="ui_memory_budget";return nullptr;}family=new(memory)UiVectorFamily(vfs);copy(family->path,sizeof family->path,full);const char*error=family->lease.add(full);if(!error)error=validateVectorFont(full);if(error){family->~UiVectorFamily();core.resources().freeAllocation(family);uiError=error;return nullptr;}family->next=uiVectorFamilies;uiVectorFamilies=family;}
 unsigned cacheGlyphs=policies.get<unsigned>("/sys/ui","font_cache_glyphs",32);
 for(auto f=family->faces;f;f=f->next)if(f->size==size&&f->cacheGlyphs==cacheGlyphs)return f->font;
 char qualified[260];snprintf(qualified,sizeof qualified,"V:%s",full);auto font=lv_tiny_ttf_create_file_ex(qualified,size,LV_FONT_KERNING_NONE,cacheGlyphs);if(!font){uiError=uiReadError?uiReadError:"font_load_failed";return nullptr;}auto memory=core.resources().allocate(sizeof(UiVectorFace));if(!memory){lv_tiny_ttf_destroy(font);uiError="ui_memory_budget";return nullptr;}auto f=new(memory)UiVectorFace{size,cacheGlyphs,font,family->faces};family->faces=f;return font;
}
