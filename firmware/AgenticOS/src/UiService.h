// Brookesia JSON-UI 0.1.x compatibility subset, rendered by the upstream LVGL library.
// Unsupported node/style/resource features are reported, never silently advertised.
static lv_display_t* uiDisplay=nullptr;static lv_obj_t* uiRoot=nullptr;static uint8_t uiDrawBuffer[board::WIDTH*8*2];
struct UiSink {void*context;void(*write)(void*,const lv_area_t&,const uint16_t*);};
static UiSink*uiSink=nullptr;static char uiBase[256]{},uiCurrent[256]{},uiPresented[256]{};
static char uiHistory[16][256]{};static unsigned uiDepth=0;static const char*uiError="none";
static const char*uiReadError=nullptr;
static uint32_t uiLeaseOwner=0;static unsigned uiNodes=0;static uint32_t uiRules[16]{};static size_t uiRuleCount=0;
static lv_font_t localFonts[7]{};static constexpr uint8_t fontSizes[]={16,20,24,32,40,48,60};
struct FontFile{char name[48]{},path[256]{};uint32_t size=0;lv_font_t*font=nullptr;};static FontFile fontFiles[4];
struct LvFile{FileProvider*p;char relative[256];uint64_t at;};
static const char*loadJsonFile(const char*path,JsonDocument&doc){
 char full[256];const char*rel;FileProvider*p;if(!resolveFile(path,full,rel,p))return "invalid_ui_path";
 FileInfo info;if(auto e=p->stat(rel,info))return e;if(info.directory||!info.size||info.size>16384)return "ui_document_limit";
 char*text=(char*)malloc(size_t(info.size)+1);if(!text)return "memory_low";size_t got=0;auto e=p->read(rel,0,(uint8_t*)text,info.size,got);
 if(!e&&got!=info.size)e="short_read";if(!e){text[got]=0;if(deserializeJson(doc,text,got,DeserializationOption::NestingLimit(16)))e="invalid_json";}free(text);return e;
}
static bool lvGlyph(const lv_font_t*f,lv_font_glyph_dsc_t*d,uint32_t cp,uint32_t){nm::FontMetrics m;uint8_t size=uintptr_t(f->dsc);if(!nm::nativeGlyphMetrics(cp,size,m))return false;d->adv_w=m.advance;d->box_w=m.w;d->box_h=m.h;d->ofs_x=m.x;d->ofs_y=f->line_height-f->base_line-m.y-m.h;d->format=LV_FONT_GLYPH_FORMAT_A8;d->gid.index=cp;d->stride=0;return true;}
static const void*lvBitmap(lv_font_glyph_dsc_t*d,lv_draw_buf_t*buf){if(!buf)return nullptr;size_t stride=lv_draw_buf_width_to_stride(d->box_w,LV_COLOR_FORMAT_A8);return nm::nativeGlyphAlpha(d->gid.index,uintptr_t(d->resolved_font->dsc),buf->data,stride)?buf:nullptr;}
static const lv_font_t*selectFont(unsigned size,const char*name="system"){
 if(strcmp(name,"system")){for(auto&f:fontFiles)if(f.font&&!strcmp(f.name,name)&&f.size==size)return f.font;return nullptr;}
 for(unsigned i=0;i<7;++i)if(size==fontSizes[i])return &localFonts[i];return nullptr;
}
static int dimension(JsonVariantConst v,int fallback){if(v.is<int>())return v.as<int>();if(v.is<const char*>()){const char*s=v.as<const char*>();char*e;long n=strtol(s,&e,10);if(e!=s&&(!strcmp(e,"dp")||!strcmp(e,"sp")||!*e))return int(n);}return fallback;}
static uint32_t rgb(const char*s,uint32_t fallback){if(!s||s[0]!='#'||strlen(s)!=7)return fallback;char*e;auto n=strtoul(s+1,&e,16);return *e?fallback:uint32_t(n);}
static void uiFlush(lv_display_t*d,const lv_area_t*a,uint8_t*p){
 if(uiSink)uiSink->write(uiSink->context,*a,reinterpret_cast<const uint16_t*>(p));
 lv_display_flush_ready(d);
}
static void initUi(){if(uiDisplay)return;lv_init();lv_tick_set_cb([]()->uint32_t{return millis();});
 for(unsigned i=0;i<7;++i){auto&f=localFonts[i];f.get_glyph_dsc=lvGlyph;f.get_glyph_bitmap=lvBitmap;f.line_height=fontSizes[i]+8;f.base_line=4;f.dsc=(void*)(uintptr_t)fontSizes[i];}
 static lv_fs_drv_t fs;lv_fs_drv_init(&fs);fs.letter='V';
 fs.open_cb=[](lv_fs_drv_t*,const char*path,lv_fs_mode_t mode)->void*{if(mode&LV_FS_MODE_WR)return nullptr;char full[256];const char*rel;FileProvider*p;if(!resolveFile(path,full,rel,p)){uiReadError="asset_path";return nullptr;}FileInfo info;if(auto e=p->stat(rel,info)){uiReadError=e;return nullptr;}if(info.directory){uiReadError="is_directory";return nullptr;}auto f=new(std::nothrow)LvFile;if(!f)return nullptr;f->p=p;copy(f->relative,sizeof(f->relative),rel);f->at=0;return f;};
 fs.close_cb=[](lv_fs_drv_t*,void*f)->lv_fs_res_t{delete (LvFile*)f;return LV_FS_RES_OK;};
 fs.read_cb=[](lv_fs_drv_t*,void*x,void*out,uint32_t n,uint32_t*got)->lv_fs_res_t{auto f=(LvFile*)x;size_t z=0;memset(out,0,n);auto e=f->p->read(f->relative,f->at,(uint8_t*)out,n,z);*got=z;f->at+=z;if(e)uiReadError=e;return e?LV_FS_RES_FS_ERR:LV_FS_RES_OK;};
 fs.seek_cb=[](lv_fs_drv_t*,void*x,uint32_t at,lv_fs_whence_t whence)->lv_fs_res_t{auto f=(LvFile*)x;FileInfo info;if(whence==LV_FS_SEEK_SET)f->at=at;else if(whence==LV_FS_SEEK_CUR)f->at+=at;else{if(f->p->stat(f->relative,info))return LV_FS_RES_FS_ERR;f->at=info.size+at;}return f->at<=UINT32_MAX?LV_FS_RES_OK:LV_FS_RES_INV_PARAM;};
 fs.tell_cb=[](lv_fs_drv_t*,void*x,uint32_t*at)->lv_fs_res_t{*at=((LvFile*)x)->at;return LV_FS_RES_OK;};lv_fs_drv_register(&fs);
 uiDisplay=lv_display_create(board::WIDTH,board::HEIGHT);lv_display_set_color_format(uiDisplay,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(uiDisplay,uiDrawBuffer,nullptr,sizeof(uiDrawBuffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(uiDisplay,uiFlush);uiRoot=lv_screen_active();lv_obj_set_style_bg_color(uiRoot,lv_color_white(),0);
}
static bool buildNode(JsonObjectConst node,lv_obj_t*parent,unsigned depth=0){
 if(++uiNodes>32||depth>8){uiError="ui_node_budget";return false;}
 const char*type=node["type"]|"";bool root=!strcmp(type,"viewScreen"),label=!strcmp(type,"label"),image=!strcmp(type,"image");
 if(!root&&!label&&!image&&strcmp(type,"container")&&strcmp(type,"button")){uiError="ui_node_not_supported";return false;}
 auto o=root?parent:label?lv_label_create(parent):image?lv_image_create(parent):!strcmp(type,"button")?lv_button_create(parent):lv_obj_create(parent);
 if(!o){uiError="ui_memory_low";return false;}lv_obj_remove_flag(o,LV_OBJ_FLAG_SCROLLABLE);
 if(node["events"].size()||node["bindings"].size()){uiError="use_app_event_bindings";return false;}
 auto style=node["style"].as<JsonObjectConst>();auto place=node["placement"].as<JsonObjectConst>();auto layout=node["layout"].as<JsonObjectConst>();
 const char*allowedStyle="bgColor,bgOpacity,textColor,textAlign,fontSize,font,padding,paddingLeft,paddingRight,paddingTop,paddingBottom,radius,borderWidth,borderColor,imageOpacity";
 for(auto p:style)if(!Policies::choice(allowedStyle,p.key().c_str())){uiError="ui_style_not_supported";return false;}
 if(!style["bgColor"].isNull()){lv_obj_set_style_bg_color(o,lv_color_hex(rgb(style["bgColor"],0xffffff)),0);lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);}
 if(!style["bgOpacity"].isNull()){int opacity=dimension(style["bgOpacity"],255);if(opacity<0||opacity>255){uiError="opacity_range";return false;}lv_obj_set_style_bg_opa(o,opacity,0);}
 lv_obj_set_style_text_color(o,lv_color_hex(rgb(style["textColor"]|"",0)),0);
 const char*align=style["textAlign"]|"left";if(!Policies::choice("left,center,right",align)){uiError="text_alignment";return false;}lv_obj_set_style_text_align(o,!strcmp(align,"center")?LV_TEXT_ALIGN_CENTER:!strcmp(align,"right")?LV_TEXT_ALIGN_RIGHT:LV_TEXT_ALIGN_LEFT,0);
 lv_obj_set_style_pad_all(o,dimension(style["padding"],root?24:0),0);lv_obj_set_style_radius(o,dimension(style["radius"],0),0);
 if(!style["paddingLeft"].isNull())lv_obj_set_style_pad_left(o,dimension(style["paddingLeft"],0),0);
 if(!style["paddingRight"].isNull())lv_obj_set_style_pad_right(o,dimension(style["paddingRight"],0),0);
 if(!style["paddingTop"].isNull())lv_obj_set_style_pad_top(o,dimension(style["paddingTop"],0),0);
 if(!style["paddingBottom"].isNull())lv_obj_set_style_pad_bottom(o,dimension(style["paddingBottom"],0),0);
 lv_obj_set_style_border_width(o,dimension(style["borderWidth"],0),0);lv_obj_set_style_border_color(o,lv_color_hex(rgb(style["borderColor"]|"",0)),0);
 unsigned size=dimension(style["fontSize"],24);const char*fontName=style["font"]|"system";char fontRef[48];if(!strncmp(fontName,"${font.",7)&&strlen(fontName)>8&&fontName[strlen(fontName)-1]=='}'){size_t n=strlen(fontName)-8;if(n>=sizeof(fontRef)){uiError="font_reference_limit";return false;}memcpy(fontRef,fontName+7,n);fontRef[n]=0;fontName=fontRef;}auto font=selectFont(size,fontName);if(!font){uiError="font_or_size_not_registered";return false;}lv_obj_set_style_text_font(o,font,0);if(strcmp(fontName,"system")&&uiLeaseOwner){for(auto&f:fontFiles)if(f.font==font&&!vfs.lockRead(f.path,uiLeaseOwner)){uiError="font_lease_capacity";return false;}}
 if(label){const char*text=node["labelProps"]["text"]|"";if(!strcmp(fontName,"system")&&!nm::localTextSupported(text,size)){uiError="system_font_missing_glyph";return false;}lv_label_set_text(o,text);lv_obj_set_width(o,LV_PCT(100));}
 if(image){const char*src=node["imageProps"]["src"]|"";char full[256];if(!normalizePath(uiBase,src,full,sizeof(full))){uiError="invalid_image_path";return false;}if(uiLeaseOwner&&!vfs.lockRead(full,uiLeaseOwner)){uiError="asset_lease_capacity";return false;}const char*relative;auto provider=vfs.resolve(full,relative);FileInfo info;if(!provider){uiError="image_not_found";return false;}if(auto e=provider->stat(relative,info)){uiError=e;return false;}if(info.directory){uiError="is_directory";return false;}
 char qualified[260];snprintf(qualified,sizeof(qualified),"V:%s",full);lv_image_header_t hdr{};uiReadError=nullptr;if(lv_image_decoder_get_info(qualified,&hdr)!=LV_RESULT_OK){uiError=uiReadError?uiReadError:"image_decode_not_available";return false;}if(!hdr.w||!hdr.h||hdr.w>2048||hdr.h>2048){uiError="image_dimensions_limit";return false;}if(strstr(src,".png")&&(uint64_t(hdr.w)*hdr.h*4>16384||info.size>16384)){uiError="large_png_requires_streamable_asset";return false;}lv_image_set_src(o,qualified);if(!style["imageOpacity"].isNull()){int opacity=dimension(style["imageOpacity"],255);if(opacity<0||opacity>255){uiError="opacity_range";return false;}lv_obj_set_style_image_opa(o,opacity,0);}}
 if(!root){if(!place["width"].isNull())lv_obj_set_width(o,dimension(place["width"],LV_SIZE_CONTENT));else if(!label&&!image)lv_obj_set_width(o,LV_PCT(100));
  if(!place["height"].isNull())lv_obj_set_height(o,dimension(place["height"],LV_SIZE_CONTENT));else lv_obj_set_height(o,LV_SIZE_CONTENT);
  if(!strcmp(place["align"]|"","center"))lv_obj_center(o);if(!place["x"].isNull()||!place["y"].isNull())lv_obj_set_pos(o,dimension(place["x"],0),dimension(place["y"],0));}
 const char*lt=layout["type"]|"none";if(!strcmp(lt,"flex")){const char*flow=layout["flexFlow"]|"column";lv_obj_set_flex_flow(o,!strcmp(flow,"row")?LV_FLEX_FLOW_ROW:LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(o,dimension(layout["gap"],0),0);lv_obj_set_style_pad_column(o,dimension(layout["gap"],0),0);}else if(strcmp(lt,"none")){uiError="layout_not_supported";return false;}
 for(auto child:node["children"].as<JsonArrayConst>())if(!buildNode(child.as<JsonObjectConst>(),o,depth+1))return false;return true;
}
static bool prepareUi(const char*path){
#ifdef ARDUINO_ARCH_ESP32
 // Admission headroom for the candidate tree, JSON and decoder scratch. This
 // is conservative, not a measurement of the entire UI's peak allocation.
 if(ESP.getFreeHeap()<65536||ESP.getMaxAllocHeap()<16384){uiError="ui_memory_budget";return false;}
#endif
 initUi();JsonDocument d;if(auto e=loadJsonFile(path,d)){uiError=e;return false;}
 const char*version=d["version"]|"";if(strcmp(version,"0.1.0")&&strcmp(version,"0.1.1")){uiError="unsupported_ui_version";return false;}
 if(d["variants"].size()){uiError="variants_not_supported_in_this_build";return false;}
 char rootBase[256];copy(rootBase,sizeof(rootBase),path);char*slash=strrchr(rootBase,'/');if(slash)*slash=0;
 auto candidate=lv_obj_create(nullptr);if(!candidate){uiError="ui_memory_low";return false;}
 unsigned oldNodes=uiNodes;uiNodes=0;uiError="none";bool found=false,ok=true;
 lv_image_cache_drop(nullptr);
 for(auto a:d["assets"].as<JsonArrayConst>()){
   JsonDocument external;JsonObjectConst obj;copy(uiBase,sizeof(uiBase),rootBase);
   if(a.is<const char*>()){
     char full[256];if(!normalizePath(rootBase,a.as<const char*>(),full,sizeof(full))){uiError="asset_path";ok=false;break;}
     if(uiLeaseOwner&&!vfs.lockRead(full,uiLeaseOwner)){uiError="asset_lease_capacity";ok=false;break;}
     if(auto e=loadJsonFile(full,external)){uiError=e;ok=false;break;}
     obj=external.as<JsonObjectConst>();copy(uiBase,sizeof(uiBase),full);if(auto end=strrchr(uiBase,'/'))*end=0;
   }else obj=a.as<JsonObjectConst>();
   if(strcmp(obj["type"]|"","viewScreen")){uiError="asset_kind_not_supported";ok=false;break;}
   if(found){uiError="multiple_screens_require_selector";ok=false;break;}
   found=true;if(!buildNode(obj,candidate)){ok=false;break;}
 }
 if(!found&&ok){uiError="screen_missing";ok=false;}
 if(!ok){lv_obj_delete(candidate);uiNodes=oldNodes;return false;}
 auto previous=uiRoot;uiRoot=candidate;lv_screen_load(uiRoot);lv_obj_delete(previous);lv_obj_update_layout(uiRoot);return true;
}
static bool renderUiRegion(const lv_area_t&area,UiSink&sink){if(!uiDisplay)return false;uiReadError=nullptr;uiSink=&sink;lv_obj_invalidate_area(uiRoot,&area);lv_refr_now(uiDisplay);uiSink=nullptr;if(uiReadError){uiError=uiReadError;lv_image_cache_drop(nullptr);for(auto&f:fontFiles)if(f.font)lv_tiny_ttf_set_size(f.font,f.size);return false;}return true;}

static const char* queueUi(const char*,bool,bool,JsonObject,const char*scope=appScope);
static bool uiUsesFont(lv_obj_t*node,const lv_font_t*font){if(!node)return false;if(lv_obj_get_style_text_font(node,LV_PART_MAIN)==font)return true;for(uint32_t i=0;i<lv_obj_get_child_count(node);++i)if(uiUsesFont(lv_obj_get_child(node,i),font))return true;return false;}
static const char* uiHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"read")||!strcmp(m,"capabilities")){out["path"]=uiCurrent;out["presented_path"]=uiPresented;out["history_depth"]=uiDepth;out["renderer"]="lvgl-9.3";out["format"]="brookesia-json-ui-0.1.x-subset";out["nodes"]="viewScreen,container,label,button,image";out["layout"]="absolute,flex-row,flex-column";out["images"]="png,jpeg,lvgl-bin";out["max_document_bytes"]=16384;out["max_nodes"]=32;out["png_decode_budget_bytes"]=16384;out["svg"]= "host_compile_to_streamable_bin";out["styles"]="bgColor,bgOpacity,textColor,textAlign,fontSize,font,padding,paddingLeft,paddingRight,paddingTop,paddingBottom,radius,borderWidth,borderColor,imageOpacity";out["draw_buffer_bytes"]=sizeof(uiDrawBuffer);out["output_color_format"]="rgb565";out["dynamic_document_reload"]=true;out["recompile_for_ui_change"]=false;out["error"]=uiError;return nullptr;}
 if(!strcmp(m,"fonts")){auto arr=out["fonts"].to<JsonArray>();for(auto size:fontSizes){auto f=arr.add<JsonObject>();f["id"]="system";f["size"]=size;f["storage"]="internal_flash";}for(auto&f:fontFiles)if(f.font){auto x=arr.add<JsonObject>();x["id"]=f.name;x["size"]=f.size;x["file"]=f.path;}return nullptr;}
 if(!strcmp(m,"font_unregister")){const char*name=a["id"]|"";unsigned size=a["size"]|0u;for(auto&f:fontFiles)if(f.font&&!strcmp(f.name,name)&&f.size==size){if(uiUsesFont(uiRoot,f.font))return "font_in_use";lv_tiny_ttf_destroy(f.font);vfs.release(0xff000000u+unsigned(&f-fontFiles));f={};return nullptr;}return "font_not_found";}
 if(!strcmp(m,"font_register")){initUi();const char*name=a["id"]|"",*path=a["file"]|"";unsigned size=a["size"]|24u;if(!*name||strlen(name)>47||size<8||size>96)return "font_parameters";
  FontFile*slot=nullptr;for(auto&f:fontFiles){if(f.font&&!strcmp(f.name,name)&&f.size==size)return "font_already_registered";if(!f.font&&!slot)slot=&f;}if(!slot)return "font_capacity";
  char full[256];if(!normalizePath("/",path,full,sizeof(full)))return "font_path";char uri[260];snprintf(uri,sizeof(uri),"V:%s",full);uint32_t owner=0xff000000u+unsigned(slot-fontFiles);if(!vfs.lockRead(full,owner))return "font_lease_capacity";uiReadError=nullptr;auto font=lv_tiny_ttf_create_file(uri,size);if(!font||uiReadError){if(font)lv_tiny_ttf_destroy(font);vfs.release(owner);return uiReadError?uiReadError:"font_load_failed";}copy(slot->name,sizeof(slot->name),name);copy(slot->path,sizeof(slot->path),full);slot->font=font;slot->size=size;return nullptr;
 }
 if(!strcmp(m,"open")||!strcmp(m,"refresh")||!strcmp(m,"back")){
  char next[256];const char*requested=!strcmp(m,"refresh")?uiCurrent:!strcmp(m,"back")?(uiDepth?uiHistory[uiDepth-1]:""):(a["path"]|"");if(!*requested)return "navigation_empty";
  if(!normalizePath("/",requested,next,sizeof(next)))return "invalid_ui_path";
  JsonDocument d;if(auto e=loadJsonFile(next,d))return e;
  JsonDocument config;policies.resolve("/dev/display",a["scope"]|appScope,config.to<JsonObject>());const char*driver=a["driver"]|(config["driver"]|"e6.ink6.ui-v1");if(strcmp(driver,"e6.ink6.ui-v1")&&strcmp(driver,"e6.ink6.image-v1"))return "unsupported_display_driver";
  bool push=!strcmp(m,"open")&&strcmp(a["history"]|"push","replace")&&uiCurrent[0];if(push&&uiDepth==16)return "history_capacity";
  if(auto e=queueUi(next,!strcmp(driver,"e6.ink6.image-v1"),a["force"]|false,out,a["scope"]|appScope))return e;
  if(push)copy(uiHistory[uiDepth++],256,uiCurrent);if(!strcmp(m,"back"))--uiDepth;copy(uiCurrent,sizeof(uiCurrent),next);return nullptr;
 }
 if(!strcmp(m,"app_load")){
  const char*file=a["file"]|"/system/default-app.json";JsonDocument d;if(auto e=loadJsonFile(file,d))return e;if(strcmp(d["format"]|"","notes-app/1"))return "app_format";
  if(d["bindings"].size()>16)return "app_binding_limit";
  const char*scope=d["policy_scope"]|"system";if(!*scope||strlen(scope)>=sizeof(appScope))return "scope_limit";
  char base[256],entry[256];copy(base,sizeof(base),file);auto slash=strrchr(base,'/');if(slash)*slash=0;if(!normalizePath(base,d["entry"]|"",entry,sizeof(entry)))return "app_entry_path";
  JsonDocument check;if(auto e=loadJsonFile(entry,check))return e;
  // Core validates the complete replacement and restores the old group on failure.
  for(auto binding:d["bindings"].as<JsonArrayConst>()){if(!binding.is<JsonObjectConst>())return "invalid_rule";}
  JsonDocument updated;auto ids=updated.to<JsonArray>();if(auto e=core.replaceRules(d["bindings"].as<JsonArrayConst>(),uiRules,uiRuleCount,ids))return e;
  uiRuleCount=ids.size();for(size_t i=0;i<uiRuleCount;++i)uiRules[i]=ids[i];copy(appScope,sizeof(appScope),scope);applyInputPolicy();
  // Loading and navigating are separate operations. An admitted app remains queryable
  // even if the display queue is full; callers can open its entry when ready.
  out["entry"]=entry;out["policy_scope"]=appScope;out["binding_count"]=uiRuleCount;out["status"]="loaded";return nullptr;
 }
 return "unsupported_method";
}
