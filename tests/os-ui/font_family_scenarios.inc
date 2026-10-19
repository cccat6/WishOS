// Real TrueType data on private native volumes, with provider generations and
// current leases. This exercises cache lifetimes independently of page names.
static bool fontCacheMounted=true,fontReplacementMounted=true;
static char fontCacheDirectory[]="/tmp/wishos-font-cache-XXXXXX",fontReplacementDirectory[]="/tmp/wishos-font-replacement-XXXXXX";
class CachedFontVolume:public PosixFileProvider {
public:
 size_t reads=0;bool tracked=true,bumpDuringRead=false;
 CachedFontVolume(const char*id,const char*mount,bool&mounted,const char*directory):PosixFileProvider(id,mount,"host_font",true,mounted,directory,"","host",512){}
 uint64_t contentRevision()const override{return tracked?changeRevision():0;}
 const char*read(const char*path,uint64_t offset,uint8_t*out,size_t n,size_t&got)override{++reads;auto error=PosixFileProvider::read(path,offset,out,n,got);if(bumpDuringRead){bumpDuringRead=false;volume("host",512);}return error;}
};
static CachedFontVolume cachedFontVolume("font-cache","/font-cache",fontCacheMounted,fontCacheDirectory),replacementFontVolume("font-replacement","/font-replacement",fontReplacementMounted,fontReplacementDirectory);
static void fontFamilyScenarios(){
 resetUiRenderer();JsonDocument snapshot,saved;policies.read("system",snapshot.to<JsonObject>());if(snapshot["overrides"].size())saved["system"]=snapshot["overrides"];else saved.to<JsonObject>();std::string originalPolicy;serializeJson(saved,originalPolicy);
 assert(mkdtemp(fontCacheDirectory)&&mkdtemp(fontReplacementDirectory));assert(vfs.add(cachedFontVolume)&&vfs.add(replacementFontVolume));
 auto original=std::filesystem::path("hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf");
 for(auto directory:{fontCacheDirectory,fontReplacementDirectory})for(auto name:{"a.ttf","b.ttf","c.ttf"})std::filesystem::copy_file(original,std::filesystem::path(directory)/name);
 JsonDocument config,out;auto settings=config["changes"].to<JsonArray>().add<JsonObject>();settings["resource"]="/sys/ui";settings["settings"]["font_face_cache"]=2;settings["settings"]["font_cache_glyphs"]=128;assert(!policies.apply(config.as<JsonObjectConst>(),out.to<JsonObject>()));
 auto document=[](const char*font,const char*text="WishOS Home",bool twice=false){JsonDocument doc;doc["version"]="0.1.1";auto screen=doc["assets"].to<JsonArray>().add<JsonObject>();screen["type"]="viewScreen";auto children=screen["children"].to<JsonArray>();for(unsigned i=0;i<(twice?2u:1u);++i){auto label=children.add<JsonObject>();label["type"]="label";label["style"]["font"]=font;label["style"]["fontSize"]=!strcmp(font,"builtin")?16:19;label["labelProps"]["text"]=text;}return doc;};
 auto builtin=document("builtin");auto prepare=[&](const char*path,bool validation=false){auto doc=document(path);return prepareUiDocument("/font-family.json",doc.as<JsonObjectConst>(),UiView{},validation);};
 auto showBuiltin=[&](){assert(prepareUiDocument("/builtin.json",builtin.as<JsonObjectConst>(),UiView{},false));};
 auto render=[](){UiSink sink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};assert(renderUiRegion({0,0,boardpack::DISPLAY_WIDTH-1,63},sink));};
 auto active=[](const char*path)->const lv_font_t*{for(auto family=uiVectorFamilies;family;family=family->next)if(!strcmp(family->path,path))for(auto face=family->faces;face;face=face->next)if(uiUsesFont(uiRoot,face->font))return face->font;return nullptr;};
 auto cached=[](const char*path){for(auto family=uiVectorFamilies;family;family=family->next)if(!strcmp(family->path,path)&&family->faces)return true;return false;};
 auto inactive=[](){unsigned n=0;for(auto family=uiVectorFamilies;family;family=family->next)for(auto face=family->faces;face;face=face->next)if(!uiUsesFont(uiRoot,face->font))++n;return n;};
 assert(prepare("/font-cache/a.ttf"));render();auto first=active("/font-cache/a.ttf");assert(first&&!vfs.canMutate("/font-cache/a.ttf"));showBuiltin();assert(cached("/font-cache/a.ttf")&&vfs.canMutate("/font-cache/a.ttf")&&!vfs.hasLeasesUnder("/font-cache"));auto before=cachedFontVolume.reads;
 assert(prepare("/font-cache/a.ttf"));render();assert(active("/font-cache/a.ttf")==first&&cachedFontVolume.reads==before);showBuiltin();
 for(auto path:{"/font-cache/b.ttf","/font-cache/c.ttf"}){assert(prepare(path));render();showBuiltin();}assert(inactive()==2&&!cached("/font-cache/a.ttf")&&cached("/font-cache/b.ttf")&&cached("/font-cache/c.ttf"));
 assert(prepare("/font-cache/b.ttf"));showBuiltin();assert(prepare("/font-cache/a.ttf"));showBuiltin();assert(inactive()==2&&cached("/font-cache/a.ttf")&&cached("/font-cache/b.ttf")&&!cached("/font-cache/c.ttf"));
 auto kept=uiRoot;{Vfs::Access writer(vfs,true);assert(!writer.add("/font-cache/a.ttf"));assert(!prepare("/font-cache/a.ttf")&&uiRoot==kept);}assert(prepare("/font-cache/a.ttf"));showBuiltin();
 // Updating an idle font is permitted. A new generation must validate its real
 // header, while failed candidates keep the builtin screen and release leases.
 {Vfs::Access writer(vfs,true);assert(!writer.add("/font-cache/a.ttf"));size_t wrote;assert(!cachedFontVolume.write("/a.ttf",0,reinterpret_cast<const uint8_t*>("BAD!"),4,false,wrote));}
 kept=uiRoot;assert(!prepare("/font-cache/a.ttf")&&uiRoot==kept&&!strcmp(uiError,"font_requires_truetype_outlines")&&vfs.canMutate("/font-cache/a.ttf"));
 {Vfs::Access writer(vfs,true);assert(!writer.add("/font-cache/a.ttf"));uint8_t signature[]={0,1,0,0};size_t wrote;assert(!cachedFontVolume.write("/a.ttf",0,signature,4,false,wrote));}
 before=cachedFontVolume.reads;assert(prepare("/font-cache/a.ttf")&&cachedFontVolume.reads>before);showBuiltin();
 {Vfs::Access writer(vfs,true);assert(!writer.add("/font-cache/a.ttf"));assert(!cachedFontVolume.remove("/a.ttf"));}kept=uiRoot;assert(!prepare("/font-cache/a.ttf")&&uiRoot==kept&&!strcmp(uiError,"not_found"));std::filesystem::copy_file(original,std::filesystem::path(fontCacheDirectory)/"a.ttf");cachedFontVolume.volume("host",512);assert(prepare("/font-cache/a.ttf"));showBuiltin();
 assert(!vfs.hasLeasesUnder("/font-cache"));fontCacheMounted=false;kept=uiRoot;assert(!prepare("/font-cache/a.ttf")&&uiRoot==kept&&!strcmp(uiError,"provider_unavailable"));fontCacheMounted=true;cachedFontVolume.volume("host",512);assert(prepare("/font-cache/a.ttf"));showBuiltin();
 cachedFontVolume.mount="/font-cache-retired";replacementFontVolume.mount="/font-cache";before=replacementFontVolume.reads;assert(prepare("/font-cache/a.ttf")&&replacementFontVolume.reads>before);bool found=false;for(auto f=uiVectorFamilies;f;f=f->next)if(f->provider==&replacementFontVolume&&f->prepared==uiFontPreparation)found=true;assert(found);showBuiltin();cachedFontVolume.mount="/font-cache";replacementFontVolume.mount="/font-replacement";assert(prepare("/font-cache/a.ttf"));showBuiltin();
 // A failed/validation-only different-family candidate keeps the active TTF
 // generation alive, including its read lease and already-rasterized glyphs.
 assert(prepare("/font-cache/a.ttf"));render();auto activeRoot=uiRoot;auto activeFont=active("/font-cache/a.ttf");
 assert(prepare("/font-cache/b.ttf",true)&&uiRoot==activeRoot&&active("/font-cache/a.ttf")==activeFont&&!vfs.canMutate("/font-cache/a.ttf")&&vfs.canMutate("/font-cache/b.ttf"));render();
 auto rejected=document("/font-cache/b.ttf","\xf4\x8f\xbf\xbf");assert(!prepareUiDocument("/rejected-family.json",rejected.as<JsonObjectConst>(),UiView{},false));assert(uiRoot==activeRoot&&active("/font-cache/a.ttf")==activeFont&&!vfs.canMutate("/font-cache/a.ttf")&&vfs.canMutate("/font-cache/b.ttf"));render();
 JsonDocument tight;tight["memory_limit_bytes"]=uint32_t(core.resources().promised()+1);assert(!core.resources().configure(tight.as<JsonObjectConst>()));assert(!prepare("/font-cache/c.ttf")&&uiRoot==activeRoot&&active("/font-cache/a.ttf")==activeFont&&!vfs.canMutate("/font-cache/a.ttf")&&vfs.canMutate("/font-cache/c.ttf"));tight["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(tight.as<JsonObjectConst>()));render();showBuiltin();
 // Zero-token adapters share a face only within one preparation. The previous
 // active generation stays alive until the new candidate commits.
 cachedFontVolume.tracked=false;auto repeated=document("/font-cache/a.ttf","WishOS Home",true);assert(prepareUiDocument("/repeat.json",repeated.as<JsonObjectConst>(),UiView{},false));auto untracked=active("/font-cache/a.ttf");unsigned families=0,faces=0;for(auto f=uiVectorFamilies;f;f=f->next)if(f->prepared==uiFontPreparation){++families;for(auto face=f->faces;face;face=face->next)++faces;}assert(families==1&&faces==1);
 before=cachedFontVolume.reads;assert(prepareUiDocument("/repeat.json",repeated.as<JsonObjectConst>(),UiView{},false));assert(cachedFontVolume.reads>before&&active("/font-cache/a.ttf")!=untracked);showBuiltin();assert(!cached("/font-cache/a.ttf"));cachedFontVolume.tracked=true;
 assert(prepare("/font-cache/a.ttf"));render();showBuiltin();kept=uiRoot;assert(prepare("/font-cache/b.ttf",true)&&uiRoot==kept&&vfs.canMutate("/font-cache/b.ttf"));auto missing=document("/font-cache/b.ttf","\xf4\x8f\xbf\xbf");assert(!prepareUiDocument("/bad.json",missing.as<JsonObjectConst>(),UiView{},false)&&uiRoot==kept&&vfs.canMutate("/font-cache/b.ttf"));
 cachedFontVolume.bumpDuringRead=true;assert(!prepare("/font-cache/c.ttf")&&uiRoot==kept&&!strcmp(uiError,"font_source_changed"));
 // Cache allocation pressure is recoverable; the old screen is preserved and
 // resetting the renderer frees active/inactive faces, readers and all leases.
 JsonDocument budget;budget["memory_limit_bytes"]=uint32_t(core.resources().promised()+1);assert(!core.resources().configure(budget.as<JsonObjectConst>()));assert(!prepare("/font-cache/c.ttf")&&uiRoot==kept);budget["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(budget.as<JsonObjectConst>()));assert(prepare("/font-cache/c.ttf"));showBuiltin();
 resetUiRenderer();assert(!uiVectorFamilies&&!vfs.hasLeasesUnder("/font-cache")&&!vfs.hasLeasesUnder("/font-replacement"));assert(policies.restore(originalPolicy.c_str()));cachedFontVolume.volume("host",512);replacementFontVolume.volume("host",512);fontCacheMounted=fontReplacementMounted=false;std::filesystem::remove_all(fontCacheDirectory);std::filesystem::remove_all(fontReplacementDirectory);assert(prepareUi("/system/ui/home.json"));
 std::cout<<"Font family cache: global inactive LRU, fresh leases, generations, replacement, zero tokens, rollback and cleanup passed\n";
}
