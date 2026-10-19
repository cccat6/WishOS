// A retained tree can accept same-geometry image bindings as one prepared
// transaction. New resources/strings are admitted before any live source moves.
static bool uiTemplateMatches(const char*path,uint32_t owner,Vfs::Access&lease){
 if(!uiRoot||!uiTemplate.revision||strcmp(path,uiTemplate.path)||owner!=uiRenderedView.owner)return false;
 const char*relative;auto provider=vfs.resolve(path,relative);return provider==uiTemplate.provider&&provider->available()&&provider->contentRevision()==uiTemplate.revision&&!lease.add(path,provider->caseInsensitive());
}
static bool uiUnchangedImagesCurrent(JsonObjectConst values,Vfs::Access&documentLease){
 for(auto ref=uiNodeRefs;ref;ref=ref->next)if(lv_obj_check_type(ref->node,&lv_image_class)){
  const char*value=*ref->id?values[ref->id].as<const char*>():nullptr;bool differs=value&&(!uiValues.containsKey(ref->id)||strcmp(value,uiValues[ref->id]|""));if(differs)continue;
  const char*src=static_cast<const char*>(lv_image_get_src(ref->node));if(!src||strncmp(src,"V:",2))return false;const char*relative;auto provider=vfs.resolve(src+2,relative);if(!ref->revision||provider!=ref->provider||!provider->available()||provider->contentRevision()!=ref->revision||documentLease.add(src+2,provider->caseInsensitive()))return false;
 }
 return true;
}
// -1 selects the ordinary complete candidate path; 0 is a rejected transaction.
static bool uiFontBindingsCurrent(Vfs::Access&lease){
 for(auto&file:fontFiles)if(file.font&&!file.scalable&&uiUsesFont(uiRoot,file.font))return false;
 for(auto family=uiVectorFamilies;family;family=family->next){bool used=false;for(auto face=family->faces;face;face=face->next)used|=uiUsesFont(uiRoot,face->font);if(used){const char*relative;auto provider=vfs.resolve(family->path,relative);if(family->stale||!family->revision||provider!=family->provider||!provider->available()||provider->contentRevision()!=family->revision||lease.add(family->path,provider->caseInsensitive()))return false;}}
 return true;
}
static int updateUiImageBindings(const char*path,const UiView&view){
 Vfs::Access documentLease(vfs,false);if(!uiTemplateMatches(path,view.owner,documentLease)||view.rotation!=uiRenderedView.rotation||strcmp(view.focus,uiRenderedView.focus)||strcmp(view.hidden,uiRenderedView.hidden))return -1;
 if(uiTemplate.fontRegistry!=uiFontRegistryRevision)return -1;
 JsonDocument settings;policies.resolve("/sys/ui","system",settings.to<JsonObject>());if(settings.overflowed()){uiError="ui_memory_budget";return 0;}
 if(strcmp(settings["font_file"]|"",uiFontPolicy.file)||(settings["font_cache_glyphs"]|32u)!=uiFontPolicy.cacheGlyphs||(settings["font_face_cache"]|0u)!=uiFontPolicy.cacheFaces)return -1;
 if(!uiFontBindingsCurrent(documentLease))return -1;
 JsonDocument values;if(deserializeJson(values,view.values)){uiError="ui_values_invalid";return 0;}
 unsigned changed=0;for(auto pair:uiValues.as<JsonObjectConst>())if(!values.containsKey(pair.key()))return -1;
 for(auto pair:values.as<JsonObjectConst>())if(!uiValues.containsKey(pair.key())||strcmp(pair.value().as<const char*>(),uiValues[pair.key()]|"")){UiNodeRef*ref=uiNodeRefs;while(ref&&strcmp(ref->id,pair.key().c_str()))ref=ref->next;if(!ref||!lv_obj_check_type(ref->node,&lv_image_class))return -1;++changed;}if(!changed)return -1;
 if(!uiUnchangedImagesCurrent(values.as<JsonObjectConst>(),documentLease))return -1;
 struct Patch{UiNodeRef*ref;char*source;FileProvider*provider;uint64_t revision;Patch*next;};Patch*patches=nullptr;
 auto clear=[&](){while(patches){auto next=patches->next;lv_free(patches->source);core.resources().freeAllocation(patches);patches=next;}};
 clearUiMemoryFailure();UiDrawBufferCandidate drawBuffer;if(!drawBuffer.prepare())return 0;applyUiImageCachePolicy();uiReadError=nullptr;UiImageAdmission source;
 for(auto ref=uiNodeRefs;ref;ref=ref->next)if(lv_obj_check_type(ref->node,&lv_image_class)){
  const char*value=*ref->id?values[ref->id].as<const char*>():nullptr;bool differs=value&&(!uiValues.containsKey(ref->id)||strcmp(value,uiValues[ref->id]|""));const char*old=static_cast<const char*>(lv_image_get_src(ref->node));char full[256];
  if(differs){if(!normalizePath(uiBase,value,full,sizeof full)){uiError="invalid_image_path";clear();return 0;}}
  else copy(full,sizeof full,old+2);
  const char*relative;auto provider=vfs.resolve(full,relative);if(provider)if(auto error=documentLease.add(full,provider->caseInsensitive())){uiError=error;clear();return 0;}
  source={};if(!admitUiImage(full,source)){clear();return 0;}auto image=reinterpret_cast<lv_image_t*>(ref->node);if(source.header.w!=image->w||source.header.h!=image->h||source.header.cf!=image->cf){clear();return -1;}
  if(differs){auto block=core.resources().allocate(sizeof(Patch));if(!block){uiError="ui_memory_budget";clear();return 0;}auto copy=lv_strdup(source.qualified);if(!copy){core.resources().freeAllocation(block);uiError="ui_memory_budget";clear();return 0;}patches=new(block)Patch{ref,copy,source.provider,source.revision,patches};}
 }
 if(uiMemoryFailed()){uiError="ui_memory_budget";clear();return 0;}
 if(!uiTemplateMatches(path,view.owner,documentLease)||!uiFontBindingsCurrent(documentLease)||!uiUnchangedImagesCurrent(values.as<JsonObjectConst>(),documentLease)){clear();return -1;}
 for(auto p=patches;p;p=p->next){const char*relative;auto provider=vfs.resolve(p->source+2,relative);if(provider!=p->provider||!provider->available()||provider->contentRevision()!=p->revision){clear();return -1;}}
 // No allocation, decoder call or layout setter remains after this boundary.
 // The fixed LVGL file-source representation owns its duplicated source string.
 drawBuffer.commit();for(auto p=patches;p;p=p->next){auto image=reinterpret_cast<lv_image_t*>(p->ref->node);auto old=const_cast<char*>(static_cast<const char*>(image->src));image->src=p->source;p->source=old;p->ref->provider=p->provider;p->ref->revision=p->revision;lv_obj_invalidate(p->ref->node);}
 swap(uiValues,values);uiRenderedView=view;uiError="none";clear();return 1;
}
