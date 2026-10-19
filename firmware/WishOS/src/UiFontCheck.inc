// Pure discovery: no LVGL initialization, face registration or presentation.
// The bound reserves escaped font/path metadata and the outer RPC envelope.
static constexpr size_t uiFontCheckLimit=(MAX_WIRE-2048)/8;
static void uiServiceDescription(JsonObject out){uiAsyncDescription(out);auto spec=out["font_check"].to<JsonObject>();spec["method"]="font_check";spec["max_codepoints"]=uiFontCheckLimit;spec["arguments"]="font?,size?,codepoints";spec["result"]="resolved,missing_codepoints,covered";spec["scope"]="read_only_current_font_resources";}
static const char*uiFontCheck(JsonObjectConst a,JsonObject out){
 if(auto e=argumentFields(a,"font,size,codepoints"))return e;auto points=a["codepoints"].as<JsonArrayConst>();if(points.isNull()||points.size()>uiFontCheckLimit)return "font_check_codepoints_limit";
 for(auto cp:points)if(!cp.is<uint32_t>()||cp.as<uint32_t>()>0x10ffff||(cp.as<uint32_t>()>=0xd800&&cp.as<uint32_t>()<=0xdfff))return "font_check_codepoint";
 if(a.containsKey("font")&&!a["font"].is<const char*>())return "ui_font_reference";
 const char*name=a["font"]|"system";auto text=a["font"].as<JsonString>();if(!*name||strlen(name)>255||(a.containsKey("font")&&strlen(name)!=text.size()))return "ui_font_reference";
 if(a.containsKey("size")&&!a["size"].is<unsigned>())return "font_size_range";unsigned size=a["size"]|24u;if(size<8||size>96)return "font_size_range";
 char path[256]{};bool builtin=false,scalable=true;
 if(!strcmp(name,"system")){BudgetJson allocator(core.resources());JsonDocument cfg(&allocator);policies.resolve("/sys/ui","system",cfg.to<JsonObject>());if(cfg.overflowed())return "ui_memory_budget";copy(path,sizeof path,cfg["font_file"]|"");builtin=!*path;}
 else if(!strcmp(name,"builtin"))builtin=true;
 else if(*name=='/'){if(!normalizePath("/",name,path,sizeof path))return "font_path";}
 else{bool found=false;for(uint32_t owner:{core.callerOwner(),uint32_t(0)}){for(auto&file:fontFiles)if(file.font&&!file.orphan&&file.owner==owner&&!strcmp(file.name,name)&&(file.size==size||(file.scalable&&!file.size))){copy(path,sizeof path,file.path);scalable=file.scalable;found=true;break;}if(found)break;}if(!found)return "font_or_size_not_registered";}
 auto resolved=out["resolved"].to<JsonObject>();resolved["font"]=name;resolved["size"]=size;
 auto missing=out["missing_codepoints"].to<JsonArray>();
 if(builtin){bool supported=false;for(auto n:fontSizes)if(size==n)supported=true;if(!supported)return "font_or_size_not_registered";resolved["format"]="builtin";resolved["source"]="internal_flash";for(auto item:points){uint32_t cp=item;nm::FontMetrics metrics;if(cp!='\n'&&cp!='\r'&&!nm::nativeGlyphMetrics(cp,size,metrics))missing.add(cp);}}
 else{const char*relative=nullptr;auto provider=vfs.resolve(path,relative);if(!provider)return "font_path";Vfs::Access lease(vfs,false);if(auto e=lease.add(path,provider->caseInsensitive()))return e;if(!provider->available())return "provider_unavailable";
  resolved["file"]=path;resolved["provider"]=provider->id;sizeText(resolved,"content_revision",provider->contentRevision());
  if(scalable){struct Reader{TrueTypeCoverage*value=nullptr;Reader(FileProvider&p,const char*rel){auto memory=core.resources().allocate(sizeof(TrueTypeCoverage));if(memory)value=new(memory)TrueTypeCoverage(p,rel);}~Reader(){if(value){value->~TrueTypeCoverage();core.resources().freeAllocation(value);}}}reader(*provider,relative);if(!reader.value)return "ui_memory_budget";
   if(auto e=reader.value->open())return e;resolved["format"]="truetype-file";resolved["cmap_format"]=reader.value->format();
   for(auto item:points){uint32_t cp=item,index=0;if(TrueTypeCoverage::zeroWidth(cp))continue;if(auto e=reader.value->glyph(cp,index))return e;if(!index)missing.add(cp);}}
  else{PackedFont reader;if(auto e=reader.open(*provider,relative))return e;resolved["format"]="nmf1-indexed-1bpp";for(auto item:points){uint32_t cp=item;if(cp=='\n'||cp=='\r')continue;PackedFont::Glyph glyph;if(auto e=reader.find(cp,size,glyph)){if(strcmp(e,"font_missing_glyph"))return e;missing.add(cp);}}}
 }
 out["covered"]=missing.size()==0;return nullptr;
}
