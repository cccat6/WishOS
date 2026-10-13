// Brookesia JSON-UI 0.1.x compatibility subset, rendered by the upstream LVGL library.
// Unsupported node/style/resource features are reported, never silently advertised.
struct UiView {uint16_t rotation=0;char values[1025]="{}",focus[49]{},hidden[385]{};};
static unsigned uiMissingGlyphs=0;
static UiView uiRenderedView;static JsonDocument uiValues;
static const char*validateUiView(JsonObjectConst a,UiView&v){
 if(a.containsKey("rotation")){if(!a["rotation"].is<uint16_t>()||a["rotation"].as<uint16_t>()>270||a["rotation"].as<uint16_t>()%90)return "ui_rotation";v.rotation=a["rotation"];}
 if(a.containsKey("focus")){if(!a["focus"].is<const char*>()||strlen(a["focus"].as<const char*>())>48)return "ui_focus";copy(v.focus,sizeof v.focus,a["focus"]);}
 if(a.containsKey("hidden")){auto ids=a["hidden"].as<JsonArrayConst>();if(ids.isNull()||ids.size()>8)return "ui_hidden_limit";v.hidden[0]=0;for(auto id:ids){if(!id.is<const char*>()||strlen(id.as<const char*>())>47||strchr(id.as<const char*>(),','))return "ui_hidden_id";if(*v.hidden)strcat(v.hidden,",");strcat(v.hidden,id.as<const char*>());}}
 if(a.containsKey("values")){auto values=a["values"].as<JsonObjectConst>();if(values.isNull()||values.size()>24||measureJson(values)>1024)return "ui_values_limit";
  for(auto f:values)if(strlen(f.key().c_str())>48||!f.value().is<const char*>())return "ui_values_type";serializeJson(values,v.values,sizeof v.values);}
 return nullptr;
}
static lv_display_t* uiDisplay=nullptr;static lv_obj_t* uiRoot=nullptr;static uint8_t uiDrawBuffer[boardpack::DISPLAY_WIDTH*8*2];
struct UiSink {void*context;void(*write)(void*,const lv_area_t&,const uint16_t*);};
static UiSink*uiSink=nullptr;static char uiBase[256]{},uiCurrent[256]{},uiPresented[256]{};
static unsigned uiDepth=0;static const char*uiError="none";
static const char*uiReadError=nullptr;
// LVGL stops drawing on decoder failure. Propagate that failure to the display
// transaction instead of committing a frame with a silently missing image.
struct UiDecoder {lv_image_decoder_t*decoder;lv_image_decoder_open_f_t open;lv_image_decoder_get_area_cb_t area;};
static UiDecoder uiDecoders[3]; // bin, JPEG, PNG in the pinned LVGL composition
static UiDecoder&uiDecoder(lv_image_decoder_t*d){for(auto&entry:uiDecoders)if(entry.decoder==d)return entry;abort();}
static void observeUiDecoders(){unsigned index=0;for(auto d=lv_image_decoder_get_next(nullptr);d;d=lv_image_decoder_get_next(d)){
 auto&entry=uiDecoders[index++];entry={d,d->open_cb,d->get_area_cb};
 lv_image_decoder_set_open_cb(d,[](lv_image_decoder_t*d,lv_image_decoder_dsc_t*s)->lv_result_t{auto r=uiDecoder(d).open(d,s);if(r!=LV_RESULT_OK&&!uiReadError)uiReadError="image_decode_failed";return r;});
 if(entry.area)lv_image_decoder_set_get_area_cb(d,[](lv_image_decoder_t*d,lv_image_decoder_dsc_t*s,const lv_area_t*full,lv_area_t*part)->lv_result_t{
  bool finished=part->x1!=LV_COORD_MIN&&part->x2>=full->x2&&part->y2>=full->y2;auto r=uiDecoder(d).area(d,s,full,part);
  if(r!=LV_RESULT_OK&&!finished&&!uiReadError)uiReadError="image_decode_failed";return r;
 });
}}
static uint32_t uiLeaseOwner=0;static unsigned uiNodes=0;static uint32_t uiRules[16]{},uiAppOwner=0;static size_t uiRuleCount=0;static char uiAppFile[256]{},uiAppEntry[256]{};
static lv_font_t localFonts[7]{};static constexpr uint8_t fontSizes[]={16,20,24,32,40,48,60};
struct FontFile{char name[48]{},path[256]{};uint32_t size=0;lv_font_t*font=nullptr;lv_font_t face{};PackedFont reader;};static FontFile fontFiles[4];
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
static void initUi(){if(uiDisplay)return;lv_init();observeUiDecoders();lv_tick_set_cb([]()->uint32_t{return millis();});
 for(unsigned i=0;i<7;++i){auto&f=localFonts[i];f.get_glyph_dsc=lvGlyph;f.get_glyph_bitmap=lvBitmap;f.line_height=fontSizes[i]+8;f.base_line=4;f.dsc=(void*)(uintptr_t)fontSizes[i];}
 static lv_fs_drv_t fs;lv_fs_drv_init(&fs);fs.letter='V';
 fs.open_cb=[](lv_fs_drv_t*,const char*path,lv_fs_mode_t mode)->void*{if(mode&LV_FS_MODE_WR)return nullptr;char full[256];const char*rel;FileProvider*p;if(!resolveFile(path,full,rel,p)){uiReadError="asset_path";return nullptr;}FileInfo info;if(auto e=p->stat(rel,info)){uiReadError=e;return nullptr;}if(info.directory){uiReadError="is_directory";return nullptr;}auto f=new(std::nothrow)LvFile;if(!f)return nullptr;f->p=p;copy(f->relative,sizeof(f->relative),rel);f->at=0;return f;};
 fs.close_cb=[](lv_fs_drv_t*,void*f)->lv_fs_res_t{delete (LvFile*)f;return LV_FS_RES_OK;};
 fs.read_cb=[](lv_fs_drv_t*,void*x,void*out,uint32_t n,uint32_t*got)->lv_fs_res_t{auto f=(LvFile*)x;size_t z=0;memset(out,0,n);auto e=f->p->read(f->relative,f->at,(uint8_t*)out,n,z);*got=z;f->at+=z;if(e)uiReadError=e;return e?LV_FS_RES_FS_ERR:LV_FS_RES_OK;};
 fs.seek_cb=[](lv_fs_drv_t*,void*x,uint32_t at,lv_fs_whence_t whence)->lv_fs_res_t{auto f=(LvFile*)x;FileInfo info;if(whence==LV_FS_SEEK_SET)f->at=at;else if(whence==LV_FS_SEEK_CUR)f->at+=at;else{if(f->p->stat(f->relative,info))return LV_FS_RES_FS_ERR;f->at=info.size+at;}return f->at<=UINT32_MAX?LV_FS_RES_OK:LV_FS_RES_INV_PARAM;};
 fs.tell_cb=[](lv_fs_drv_t*,void*x,uint32_t*at)->lv_fs_res_t{*at=((LvFile*)x)->at;return LV_FS_RES_OK;};lv_fs_drv_register(&fs);
 uiDisplay=lv_display_create(boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT);lv_display_set_color_format(uiDisplay,LV_COLOR_FORMAT_RGB565);lv_display_set_buffers(uiDisplay,uiDrawBuffer,nullptr,sizeof(uiDrawBuffer),LV_DISPLAY_RENDER_MODE_PARTIAL);lv_display_set_flush_cb(uiDisplay,uiFlush);uiRoot=lv_screen_active();lv_obj_set_style_bg_color(uiRoot,lv_color_white(),0);
}
static char* uiFallbackText(const char*text,unsigned size){
 size_t n=strlen(text);char*out=(char*)malloc(n+1);if(!out)return nullptr;size_t at=0,wrote=0;
 while(at<n){uint8_t lead=text[at];size_t length=lead<128?1:(lead&0xe0)==0xc0?2:(lead&0xf0)==0xe0?3:(lead&0xf8)==0xf0?4:1;bool valid=at+length<=n;uint32_t cp=length==1?lead:lead&((1<<(7-length))-1);
  for(size_t i=1;valid&&i<length;++i){uint8_t byte=text[at+i];valid=(byte&0xc0)==0x80;cp=(cp<<6)|(byte&63);}
  nm::FontMetrics m;bool supported=valid&&(cp==' '||cp=='\n'||cp=='\r'||nm::nativeGlyphMetrics(cp,size,m));
  if(supported){memcpy(out+wrote,text+at,length);wrote+=length;}else{out[wrote++]='?';++uiMissingGlyphs;}at+=valid?length:1;
 }out[wrote]=0;return out;
}
static bool buildNode(JsonObjectConst node,lv_obj_t*parent,unsigned depth=0){
 if(*uiRenderedView.hidden&&Policies::choice(uiRenderedView.hidden,node["id"]|""))return true;
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
 if(*uiRenderedView.focus&&!strcmp(node["id"]|"",uiRenderedView.focus)){lv_obj_set_style_border_width(o,2,0);lv_obj_set_style_border_color(o,lv_color_black(),0);}
 if(label){const char*text=uiValues[node["id"]|""]|(node["labelProps"]["text"]|"");const char*missing=node["labelProps"]["missingGlyph"]|"error";if(!Policies::choice("error,replace",missing)){uiError="missing_glyph_policy";return false;}
 if(!strcmp(fontName,"system")&&!nm::localTextSupported(text,size)){if(strcmp(missing,"replace")){uiError="system_font_missing_glyph";return false;}char*safe=uiFallbackText(text,size);if(!safe){uiError="ui_memory_low";return false;}lv_label_set_text(o,safe);free(safe);}else lv_label_set_text(o,text);lv_obj_set_width(o,LV_PCT(100));}
 if(image){const char*src=node["imageProps"]["src"]|"";char full[256];if(!normalizePath(uiBase,src,full,sizeof(full))){uiError="invalid_image_path";return false;}if(uiLeaseOwner&&!vfs.lockRead(full,uiLeaseOwner)){uiError="asset_lease_capacity";return false;}const char*relative;auto provider=vfs.resolve(full,relative);FileInfo info;if(!provider){uiError="image_not_found";return false;}if(auto e=provider->stat(relative,info)){uiError=e;return false;}if(info.directory){uiError="is_directory";return false;}
 char qualified[260];snprintf(qualified,sizeof(qualified),"V:%s",full);lv_image_header_t hdr{};uiReadError=nullptr;if(lv_image_decoder_get_info(qualified,&hdr)!=LV_RESULT_OK){uiError=uiReadError?uiReadError:"image_decode_not_available";return false;}if(!hdr.w||!hdr.h||hdr.w>2048||hdr.h>2048){uiError="image_dimensions_limit";return false;}if(strstr(src,".png")&&(uint64_t(hdr.w)*hdr.h*4>16384||info.size>16384)){uiError="large_png_requires_streamable_asset";return false;}lv_image_set_src(o,qualified);if(!style["imageOpacity"].isNull()){int opacity=dimension(style["imageOpacity"],255);if(opacity<0||opacity>255){uiError="opacity_range";return false;}lv_obj_set_style_image_opa(o,opacity,0);}}
 if(!root){if(!place["width"].isNull())lv_obj_set_width(o,dimension(place["width"],LV_SIZE_CONTENT));else if(!label&&!image)lv_obj_set_width(o,LV_PCT(100));
  if(!place["height"].isNull())lv_obj_set_height(o,dimension(place["height"],LV_SIZE_CONTENT));else lv_obj_set_height(o,LV_SIZE_CONTENT);
  if(!strcmp(place["align"]|"","center"))lv_obj_center(o);if(!place["x"].isNull()||!place["y"].isNull())lv_obj_set_pos(o,dimension(place["x"],0),dimension(place["y"],0));}
 const char*lt=layout["type"]|"none";if(!strcmp(lt,"flex")){const char*flow=layout["flexFlow"]|"column";lv_obj_set_flex_flow(o,!strcmp(flow,"row")?LV_FLEX_FLOW_ROW:LV_FLEX_FLOW_COLUMN);lv_obj_set_style_pad_row(o,dimension(layout["gap"],0),0);lv_obj_set_style_pad_column(o,dimension(layout["gap"],0),0);}else if(strcmp(lt,"none")){uiError="layout_not_supported";return false;}
 for(auto child:node["children"].as<JsonArrayConst>())if(!buildNode(child.as<JsonObjectConst>(),o,depth+1))return false;return true;
}
static size_t uiTreeMemory(JsonObjectConst node){
 // This adapter's candidate-tree allowance scales with document complexity;
 // it is neither an application quota nor an assumption about MCU capacity.
 size_t bytes=768+2*strlen(node["labelProps"]["text"]|"");
 for(auto child:node["children"].as<JsonArrayConst>())bytes+=uiTreeMemory(child.as<JsonObjectConst>());
 return bytes;
}
struct UiTreeReservation {uint32_t id;explicit UiTreeReservation(size_t n):id(core.resources().acquire(n,0)){}~UiTreeReservation(){core.resources().release(id);}};
static bool prepareUi(const char*path,const UiView&view=UiView{},bool validateOnly=false){
 initUi();JsonDocument d;if(auto e=loadJsonFile(path,d)){uiError=e;return false;}
 const char*version=d["version"]|"";if(strcmp(version,"0.1.0")&&strcmp(version,"0.1.1")){uiError="unsupported_ui_version";return false;}
 if(d["variants"].size()){uiError="variants_not_supported_in_this_build";return false;}
 char rootBase[256];copy(rootBase,sizeof(rootBase),path);char*slash=strrchr(rootBase,'/');if(slash)*slash=0;
 UiView previousView=uiRenderedView;uiRenderedView=view;
 uiValues.clear();if(deserializeJson(uiValues,view.values)){uiRenderedView=previousView;uiError="ui_values_invalid";return false;}
 lv_display_set_resolution(uiDisplay,view.rotation%180?boardpack::DISPLAY_HEIGHT:boardpack::DISPLAY_WIDTH,view.rotation%180?boardpack::DISPLAY_WIDTH:boardpack::DISPLAY_HEIGHT);
 auto candidate=lv_obj_create(nullptr);if(!candidate){uiRenderedView=previousView;lv_display_set_resolution(uiDisplay,previousView.rotation%180?boardpack::DISPLAY_HEIGHT:boardpack::DISPLAY_WIDTH,previousView.rotation%180?boardpack::DISPLAY_WIDTH:boardpack::DISPLAY_HEIGHT);uiError="ui_memory_low";return false;}
 unsigned oldNodes=uiNodes,oldMissing=uiMissingGlyphs;uiNodes=0;uiMissingGlyphs=0;uiError="none";bool found=false,ok=true;
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
   UiTreeReservation memory(2048+2*strlen(view.values)+uiTreeMemory(obj));if(!memory.id){uiError="ui_memory_budget";ok=false;break;}
   found=true;if(!buildNode(obj,candidate)){ok=false;break;}
 }
 if(!found&&ok){uiError="screen_missing";ok=false;}
 if(!ok||validateOnly){lv_obj_delete(candidate);uiNodes=oldNodes;uiMissingGlyphs=oldMissing;uiRenderedView=previousView;lv_display_set_resolution(uiDisplay,previousView.rotation%180?boardpack::DISPLAY_HEIGHT:boardpack::DISPLAY_WIDTH,previousView.rotation%180?boardpack::DISPLAY_WIDTH:boardpack::DISPLAY_HEIGHT);return ok;}
 auto previous=uiRoot;uiRoot=candidate;lv_screen_load(uiRoot);lv_obj_delete(previous);lv_obj_update_layout(uiRoot);return true;
}
static bool renderUiRegion(const lv_area_t&area,UiSink&sink){if(!uiDisplay)return false;uiReadError=nullptr;uiSink=&sink;lv_obj_invalidate_area(uiRoot,&area);lv_refr_now(uiDisplay);uiSink=nullptr;if(uiReadError){uiError=uiReadError;lv_image_cache_drop(nullptr);return false;}return true;}

static const char*defaultUiDriver();
static const char*validateUiDriver(const char*);
static const char* queueUi(const char*,const char*,bool,JsonObject,const char*scope=appScope,const UiView*view=nullptr,PresentationState::Stamp stamp={});
static PresentationState presentationState;
struct UiDocument {char path[256]{},scope[49]{},driver[48]{};UiView view;bool force=false;uint32_t memory=0,lastJob=0;PresentationState::Stamp scheduled;};
static UiDocument*uiDocuments[PresentationState::Capacity]{};
static UiDocument*uiDocument(uint32_t id){unsigned slot=presentationState.slot(id);return slot<PresentationState::Capacity?uiDocuments[slot]:nullptr;}
// Every accepted model revision has an outcome, including revisions replaced
// before a display queue could admit them. Admitted work has its driver's receipt.
static void finishDeferredUi(PresentationState::Stamp stamp,uint32_t owner,const UiDocument*doc,const char*reason){
 if(!doc||(doc->scheduled.view==stamp.view&&doc->scheduled.revision==stamp.revision))return;
 JsonDocument data;data["view_id"]=stamp.view;data["view_revision"]=stamp.revision;data["owner"]=owner;data["activation"]=stamp.activation;
 data["success"]=false;data["state"]="superseded";data["error"]=reason;data["completion"]["kind"]="none";data["completion"]["source"]="not_admitted";
 core.emit("/dev/display","failed",data.as<JsonObjectConst>(),owner);
}
static void freeUiDocument(unsigned slot){if(slot>=PresentationState::Capacity)return;auto d=uiDocuments[slot];if(d){core.resources().release(d->memory);delete d;uiDocuments[slot]=nullptr;}}
static void uiStateChanged(const char*event="activated",uint32_t view=0){uiDepth=presentationState.depth();auto d=uiDocument(presentationState.active());copy(uiCurrent,sizeof uiCurrent,d?d->path:"");JsonDocument data;data["view_id"]=view?view:presentationState.active();data["active_view"]=presentationState.active();core.emit("/sys/ui",event,data.as<JsonObjectConst>());}
static bool sameStamp(PresentationState::Stamp a,PresentationState::Stamp b){return a.view==b.view&&a.revision==b.revision&&a.activation==b.activation;}
static void pollUiPresentation(){auto id=presentationState.active();auto d=uiDocument(id);if(!d)return;auto stamp=presentationState.stamp(id);if(sameStamp(stamp,d->scheduled))return;
 JsonDocument out;if(!queueUi(d->path,d->driver,d->force,out.to<JsonObject>(),d->scope,&d->view,stamp)){d->scheduled=stamp;d->lastJob=out["job_id"]|0u;}
}
static bool initUiLifecycle(){static bool initialized=false;if(initialized)return true;initialized=core.onOwnerRelease(&presentationState,[](void*,uint32_t owner){if(uiAppOwner==owner){uiAppOwner=0;uiRuleCount=0;uiAppFile[0]=uiAppEntry[0]=0;copy(appScope,sizeof appScope,"system");}bool changed=false;for(unsigned i=0;i<PresentationState::Capacity;++i){auto v=presentationState.at(i);if(v.id&&v.owner==owner){auto stamp=presentationState.stamp(v.id);if(!presentationState.close(v.id,0)){finishDeferredUi(stamp,owner,uiDocuments[i],"view_closed");freeUiDocument(i);changed=true;}}}if(changed)uiStateChanged();});return initialized;}
static const char*displayViewStamp(JsonObjectConst a,PresentationState::Stamp&stamp){
 if(!a["view_id"].is<uint32_t>())return "view_context_required";uint32_t id=a["view_id"];if(auto e=presentationState.access(id,core.callerOwner()))return e;
 if(presentationState.get(id)->document)return "document_requires_ui_update";stamp=presentationState.stamp(id);if(!stamp.activation)return "view_not_foreground";
 if(a.containsKey("view_revision")&&(!a["view_revision"].is<uint32_t>()||a["view_revision"].as<uint32_t>()!=stamp.revision))return "view_revision_conflict";return nullptr;
}
static bool uiUsesFont(lv_obj_t*node,const lv_font_t*font){if(!node)return false;if(lv_obj_get_style_text_font(node,LV_PART_MAIN)==font)return true;for(uint32_t i=0;i<lv_obj_get_child_count(node);++i)if(uiUsesFont(lv_obj_get_child(node,i),font))return true;return false;}
static const char* uiHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"read")){out["application"]["file"]=uiAppFile;out["application"]["entry"]=uiAppEntry;out["application"]["owner"]=uiAppOwner;out["application"]["binding_count"]=uiRuleCount;}
 if(Policies::choice("create,activate,close,back,open,update,refresh",m)){
  const char*allowed=!strcmp(m,"create")?"":!strcmp(m,"activate")?"view_id,history":!strcmp(m,"close")?"view_id":!strcmp(m,"back")?"":"view_id,path,driver,scope,values,focus,hidden,rotation,force,history,expected_revision";
  for(auto f:a)if(!Policies::choice(allowed,f.key().c_str()))return "unknown_ui_argument";
  if(a.containsKey("view_id")&&(!a["view_id"].is<uint32_t>()||!a["view_id"].as<uint32_t>()))return "view_id_type";
  if(a.containsKey("history")&&(!a["history"].is<const char*>()||!Policies::choice("push,replace",a["history"])))return "ui_history_mode";
 }

 if(!strcmp(m,"read")||!strcmp(m,"capabilities")){out["path"]=uiCurrent;out["presented_path"]=uiPresented;out["history_depth"]=uiDepth;out["renderer"]="lvgl-9.3";out["format"]="brookesia-json-ui-0.1.x-subset";out["nodes"]="viewScreen,container,label,button,image";out["layout"]="absolute,flex-row,flex-column";out["images"]="png,jpeg,lvgl-bin";out["external_fonts"]="nmf1-indexed-1bpp";out["font_glyph_buffer_bytes"]=PackedFont::MAX_BITMAP_BYTES;out["max_document_bytes"]=16384;out["max_nodes"]=32;out["png_decode_budget_bytes"]=16384;out["svg"]= "host_compile_to_streamable_bin";out["styles"]="bgColor,bgOpacity,textColor,textAlign,fontSize,font,padding,paddingLeft,paddingRight,paddingTop,paddingBottom,radius,borderWidth,borderColor,imageOpacity";out["draw_buffer_bytes"]=sizeof(uiDrawBuffer);out["output_color_format"]="rgb565";out["dynamic_document_reload"]=true;out["recompile_for_ui_change"]=false;out["font_fallback_substitutions"]=uiMissingGlyphs;presentationState.describe(out["presentation"].to<JsonObject>());uint32_t target=a["view_id"]|presentationState.active();if(a.containsKey("view_id")&&(!a["view_id"].is<uint32_t>()||!presentationState.get(target)))return "view_not_found";auto active=uiDocument(target);out["view_id"]=target;if(auto item=presentationState.get(target))out["revision"]=item->revision;if(active){out["path"]=active->path;out["driver"]=active->driver;out["scheduled_revision"]=active->scheduled.revision;out["job_id"]=active->lastJob;JsonDocument values;if(!deserializeJson(values,active->view.values))out["values"]=values.as<JsonObjectConst>();}out["rotation"]=active?active->view.rotation:0;out["rendered_rotation"]=uiRenderedView.rotation;out["focus"]=active?active->view.focus:"";out["values_limit_bytes"]=1024;out["presentation_scope"]="whole_refresh";out["events"]="created,updated,activated,closed";out["update_activates_view"]=false;out["error"]=uiError;return nullptr;}
 if(!strcmp(m,"fonts")){auto arr=out["fonts"].to<JsonArray>();for(auto size:fontSizes){auto f=arr.add<JsonObject>();f["id"]="system";f["size"]=size;f["storage"]="internal_flash";}for(auto&f:fontFiles)if(f.font){auto x=arr.add<JsonObject>();x["id"]=f.name;x["size"]=f.size;x["file"]=f.path;}return nullptr;}
 if(!strcmp(m,"font_unregister")){const char*name=a["id"]|"";unsigned size=a["size"]|0u;for(auto&f:fontFiles)if(f.font&&!strcmp(f.name,name)&&f.size==size){if(uiUsesFont(uiRoot,f.font))return "font_in_use";vfs.release(0xff000000u+unsigned(&f-fontFiles));f={};return nullptr;}return "font_not_found";}
 if(!strcmp(m,"font_register")){initUi();const char*name=a["id"]|"",*path=a["file"]|"";unsigned size=a["size"]|24u;if(!*name||strlen(name)>47||size<8||size>96)return "font_parameters";
  FontFile*slot=nullptr;for(auto&f:fontFiles){if(f.font&&!strcmp(f.name,name)&&f.size==size)return "font_already_registered";if(!f.font&&!slot)slot=&f;}if(!slot)return "font_capacity";
  if(!strcmp(name,"system"))return "font_id_reserved";
  char full[256];if(!normalizePath("/",path,full,sizeof full))return "font_path";const char*relative;auto provider=vfs.resolve(full,relative);if(!provider)return "font_path";
  uint32_t owner=0xff000000u+unsigned(slot-fontFiles);if(!vfs.lockRead(full,owner))return "font_lease_capacity";
  if(auto e=slot->reader.open(*provider,relative)){vfs.release(owner);return e;}PackedFont::Glyph fallback;if(auto e=slot->reader.find('?',size,fallback)){vfs.release(owner);return e;}
  copy(slot->name,sizeof slot->name,name);copy(slot->path,sizeof slot->path,full);slot->size=size;slot->face={};slot->face.line_height=size+8;slot->face.base_line=4;slot->face.dsc=slot;
  slot->face.get_glyph_dsc=[](const lv_font_t*f,lv_font_glyph_dsc_t*d,uint32_t cp,uint32_t)->bool{auto&file=*static_cast<FontFile*>(const_cast<void*>(f->dsc));PackedFont::Glyph g;if(auto e=file.reader.find(cp,file.size,g)){uiReadError=e;return false;}d->adv_w=g.advance;d->box_w=g.w;d->box_h=g.h;d->ofs_x=g.x;d->ofs_y=f->line_height-f->base_line-g.y-g.h;d->format=LV_FONT_GLYPH_FORMAT_A8;d->gid.index=g.index;d->stride=0;return true;};
  slot->face.get_glyph_bitmap=[](lv_font_glyph_dsc_t*d,lv_draw_buf_t*buf)->const void*{if(!buf)return nullptr;auto&file=*static_cast<FontFile*>(const_cast<void*>(d->resolved_font->dsc));PackedFont::Glyph g;if(auto e=file.reader.record(d->gid.index,g)){uiReadError=e;return nullptr;}uint8_t bits[PackedFont::MAX_BITMAP_BYTES];if(auto e=file.reader.bitmap(g,bits)){uiReadError=e;return nullptr;}size_t stride=lv_draw_buf_width_to_stride(g.w,LV_COLOR_FORMAT_A8),row=(g.w+7)/8;for(unsigned y=0;y<g.h;++y)for(unsigned x=0;x<g.w;++x)buf->data[y*stride+x]=((bits[y*row+x/8]>>(7-x%8))&1)?255:0;return buf;};
  slot->font=&slot->face;return nullptr;
 }
 if(!strcmp(m,"create")){if(!initUiLifecycle())return "lifecycle_capacity";uint32_t id=0;if(auto e=presentationState.create(core.callerOwner(),false,false,id))return e;out["view_id"]=id;out["revision"]=1;out["status"]="created";uiStateChanged("created",id);return nullptr;}
 if(!strcmp(m,"activate")||!strcmp(m,"close")||!strcmp(m,"back")){
  uint32_t id=a["view_id"]|0u;const char*e=nullptr;
  if(!strcmp(m,"activate"))e=presentationState.activate(id,strcmp(a["history"]|"push","replace"),core.callerOwner());
  else if(!strcmp(m,"close")){unsigned slot=presentationState.slot(id);auto stamp=presentationState.stamp(id);auto v=presentationState.get(id);uint32_t owner=v?v->owner:0;e=presentationState.close(id,core.callerOwner());if(!e){finishDeferredUi(stamp,owner,uiDocuments[slot],"view_closed");freeUiDocument(slot);}}
  else{uint32_t old=presentationState.active();unsigned slot=presentationState.slot(old);auto stamp=presentationState.stamp(old);auto v=presentationState.get(old);uint32_t owner=v?v->owner:0;uint32_t disposed=0;e=presentationState.back(core.callerOwner(),disposed);if(!e&&disposed){finishDeferredUi(stamp,owner,uiDocuments[slot],"view_closed");freeUiDocument(slot);}}
  if(e)return e;uiStateChanged(!strcmp(m,"close")?"closed":"activated",!strcmp(m,"close")?id:0);pollUiPresentation();out["view_id"]=presentationState.active();out["status"]="applied";return nullptr;
 }
 if(!strcmp(m,"open")||!strcmp(m,"update")||!strcmp(m,"refresh")){
  if(!initUiLifecycle())return "lifecycle_capacity";uint32_t id=a["view_id"]|(!strcmp(m,"refresh")?presentationState.active():0u);
  if(!id&&!strcmp(m,"open")&&!strcmp(a["history"]|"push","replace")&&!presentationState.access(presentationState.active(),core.callerOwner())&&uiDocument(presentationState.active()))id=presentationState.active();UiDocument*prior=id?uiDocument(id):nullptr;if(id&&!prior)return "document_view_required";
  if(strcmp(m,"open")){if(auto e=presentationState.access(id,core.callerOwner()))return e;if(!prior)return "document_view_required";}
  UiDocument next;if(prior)next=*prior;
  const char*requested=a["path"]|(prior?prior->path:"");if(!*requested)return "navigation_empty";
  if(!normalizePath("/",requested,next.path,sizeof next.path))return "invalid_ui_path";
  JsonDocument document;if(auto e=loadJsonFile(next.path,document))return e;
  if(auto e=validateUiView(a,next.view))return e;
  if(a.containsKey("force")&&!a["force"].is<bool>())return "ui_force_type";next.force=a["force"]|false;
  const char*defaultScope=prior?prior->scope:appScope;const char*scope=a["scope"]|defaultScope;if(!Policies::validScope(scope))return "invalid_scope";copy(next.scope,sizeof next.scope,scope);
  JsonDocument config;policies.resolve("/dev/display",scope,config.to<JsonObject>());const char*driver=a["driver"]|(prior?prior->driver:(document["presentation"]["driver"]|(config["driver"]|defaultUiDriver())));
  if(auto e=validateUiDriver(driver))return e;if(strlen(driver)>=sizeof next.driver)return "driver_id_limit";copy(next.driver,sizeof next.driver,driver);
  uint32_t expected=a["expected_revision"]|0u;if(a.containsKey("expected_revision")&&(!a["expected_revision"].is<uint32_t>()||!expected))return "view_revision_type";if(prior&&expected!=0&&presentationState.get(id)->revision!=expected)return "view_revision_conflict";
  uint32_t previousLease=uiLeaseOwner;constexpr uint32_t validationLease=0xffffffe8u;uiLeaseOwner=validationLease;bool valid=prepareUi(next.path,next.view,true);uiLeaseOwner=previousLease;vfs.release(validationLease);if(!valid)return uiError;
  if(prior){auto stamp=presentationState.stamp(id);auto owner=presentationState.get(id)->owner;if(auto e=presentationState.revise(id,core.callerOwner(),expected))return e;finishDeferredUi(stamp,owner,prior,"view_revision_replaced");*prior=next;}
  else{
   next.memory=core.resources().acquire(sizeof(UiDocument),core.callerOwner());if(!next.memory)return "ui_memory_budget";auto model=new(std::nothrow)UiDocument(next);if(!model){core.resources().release(next.memory);return "memory_low";}core.resources().used(next.memory,sizeof(UiDocument));
   if(auto e=presentationState.create(core.callerOwner(),true,true,id)){core.resources().release(next.memory);delete model;return e;}unsigned slot=presentationState.slot(id);uiDocuments[slot]=model;
   if(auto e=presentationState.activate(id,strcmp(a["history"]|"push","replace"),core.callerOwner())){presentationState.close(id,0);freeUiDocument(slot);return e;}
  }
  uiStateChanged(prior?"updated":"activated",id);pollUiPresentation();auto stamp=presentationState.stamp(id);out["view_id"]=id;out["revision"]=stamp.revision;out["foreground"]=bool(stamp.activation);auto current=uiDocument(id);if(current&&sameStamp(current->scheduled,stamp))out["job_id"]=current->lastJob;out["status"]=stamp.activation?"updated_pending_presentation":"updated";return nullptr;
 }
 if(!strcmp(m,"app_load")){
  if(!initUiLifecycle())return "lifecycle_capacity";
  for(auto field:a)if(strcmp(field.key().c_str(),"file"))return "unknown_app_argument";
  if(!a["file"].is<const char*>())return "app_file_required";
  char file[256];if(!normalizePath("/",a["file"],file,sizeof file))return "app_file_path";
  if(core.callerOwner()&&uiAppOwner&&uiAppOwner!=core.callerOwner())return "app_owned_by_other";
  JsonDocument d;if(auto e=loadJsonFile(file,d))return e;if(strcmp(d["format"]|"","notes-app/1"))return "app_format";
  for(auto field:d.as<JsonObjectConst>())if(!Policies::choice("format,entry,bindings,policy_scope",field.key().c_str()))return "unknown_app_field";
  if(!d["entry"].is<const char*>()||!*d["entry"].as<const char*>())return "app_entry_path";
  if(d.containsKey("bindings")&&!d["bindings"].is<JsonArrayConst>())return "app_bindings_type";
  if(d.containsKey("policy_scope")&&!d["policy_scope"].is<const char*>())return "scope_type";
  if(d["bindings"].size()>16)return "app_binding_limit";
  const char*scope=d["policy_scope"]|"system";if(!Policies::validScope(scope)||strlen(scope)>=sizeof(appScope))return "scope_limit";
  char base[256],entry[256];copy(base,sizeof(base),file);auto slash=strrchr(base,'/');if(slash)*slash=0;if(!normalizePath(base,d["entry"]|"",entry,sizeof(entry)))return "app_entry_path";
  uint32_t previousLease=uiLeaseOwner;constexpr uint32_t validationLease=0xffffffe8u;uiLeaseOwner=validationLease;bool valid=prepareUi(entry,UiView{},true);uiLeaseOwner=previousLease;vfs.release(validationLease);if(!valid)return uiError;
  // Core validates the complete replacement and restores the old group on failure.
  for(auto binding:d["bindings"].as<JsonArrayConst>()){if(!binding.is<JsonObjectConst>())return "invalid_rule";}
  JsonDocument updated;auto ids=updated.to<JsonArray>();if(auto e=core.replaceRules(d["bindings"].as<JsonArrayConst>(),uiRules,uiRuleCount,ids))return e;
  uiRuleCount=ids.size();for(size_t i=0;i<uiRuleCount;++i)uiRules[i]=ids[i];copy(appScope,sizeof(appScope),scope);uiAppOwner=core.callerOwner();copy(uiAppFile,sizeof uiAppFile,file);copy(uiAppEntry,sizeof uiAppEntry,entry);
  // Loading and navigating are separate operations. An admitted app remains queryable
  // even if the display queue is full; callers can open its entry when ready.
  out["entry"]=entry;out["policy_scope"]=appScope;out["binding_count"]=uiRuleCount;out["status"]="loaded";return nullptr;
 }
 return "unsupported_method";
}
