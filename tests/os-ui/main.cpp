#include "UiMemory.h"
#include "PackedFont.h"
#include <lvgl_private.h>
#ifdef NDEBUG
#error Assertions must be enabled in this test
#endif
#include <Arduino.h>
#include <lvgl.h>
#include "Core.h"
#include "PresentationState.h"
#include "DisplayScheduler.h"
#include "drivers/e6/E6Profiles.h"
#include "ScriptHost.h"
#include "Sha256.h"
#include "display/NativeFont.h"
#include "Vfs.h"
#include "compositions/agentic-notes-e6/Defaults.h"
#include "SystemFiles.h"
#include "drivers/e6/scene/Content.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
static size_t uiAllocCalls=0,uiFailAt=SIZE_MAX;
static void*uiFaultResize(void*p,size_t n){if(!n){free(p);return nullptr;}if(uiAllocCalls++==uiFailAt)return nullptr;return realloc(p,n);}
uint32_t millis(){static uint32_t t=0;return ++t;}
namespace notesos {
static Core core;static Policies policies(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));static char appScope[48]="system";static Vfs vfs;static SystemFiles systemFiles;class CountedFiles:public MemoryFiles {public:bool failOnce=false;size_t largestRead=0,totalRead=0,readOps=0;CountedFiles():MemoryFiles("ram","/storage/ram","ram"){}const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{if(failOnce){failOnce=false;got=0;return "spi_busy";}++readOps;largestRead=std::max(largestRead,n);auto e=MemoryFiles::read(p,at,b,n,got);totalRead+=got;return e;}};
static CountedFiles files;
class CompleteFontFiles:public FileProvider {
public:
 size_t readBytes=0,largestRead=0;bool failOnce=false;
 CompleteFontFiles():FileProvider("fonts","/fonts","host_fixture",true,false,false){}
 bool available()const override{return true;}
 void describe(JsonObject o)const override{o["filesystem"]="host_font_fixture";}
 const char*write(const char*,uint64_t,const uint8_t*,size_t,bool,size_t&w)override{w=0;return "read_only";}
 const char*list(const char*,uint32_t,uint32_t,JsonObject)override{return "unsupported_operation";}
 const char*remove(const char*)override{return "read_only";}
 const char*mkdir(const char*)override{return "read_only";}
 const char*rename(const char*,const char*)override{return "read_only";}
 const char*stat(const char*path,FileInfo&info)override{if(strcmp(path,"/complete.ttf"))return "not_found";std::ifstream f("hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf",std::ios::binary|std::ios::ate);if(!f)return "not_found";info={uint64_t(f.tellg()),false};return nullptr;}
 const char*read(const char*path,uint64_t at,uint8_t*out,size_t n,size_t&got)override{got=0;if(failOnce){failOnce=false;return "font_fixture_io";}FileInfo info;if(auto e=stat(path,info))return e;std::ifstream f("hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf",std::ios::binary);f.seekg(at);f.read(reinterpret_cast<char*>(out),n);got=f.gcount();readBytes+=got;largestRead=std::max(largestRead,n);return nullptr;}
};static CompleteFontFiles completeFonts;
static bool resolveFile(const char*in,char*full,const char*&rel,FileProvider*&p){if(!normalizePath("/",in,full,256))return false;p=vfs.resolve(full,rel);return p;}
#include "UiService.h"
#include "../../hardware/agent-os/compositions/agentic-notes-e6/E6UiSurface.inc"
static unsigned uiQueueCalls=0;static bool deferUi=false;static ScriptHost*callbackRuntime=nullptr;static PresentationState::Stamp lastUiStamp;
static unsigned micStarts=0;
static const char*defaultUiDriver(){return "test.rgb565";}
static const char*validateUiDriver(const char*){return nullptr;}
static DisplayAdmission queueUi(const char*,const char*,bool,JsonObject out,const char*,const UiView*,PresentationState::Stamp stamp){if(deferUi)return DisplayAdmission::deferred("display_busy");++uiQueueCalls;lastUiStamp=stamp;out["job_id"]=uiQueueCalls;out["status"]="queued";return {};}
}
int main(){using namespace notesos;vfs.add(systemFiles);vfs.add(files);
 { JsonDocument input;nm::Scene scene;assert(!deserializeJson(input,R"({"format":"scene-v1","background":1,"commands":[{"op":"fill","x":0,"y":0,"w":40,"h":50,"color":3}]})"));assert(nm::parseScene(input.as<JsonObjectConst>(),scene));assert(scene.count==1&&scene.commands[0].color==3);input["commands"][0]["color"]=4;assert(!nm::parseScene(input.as<JsonObjectConst>(),scene));input["commands"][0]["color"]=3;input["next_poll_s"]=60;assert(!nm::parseScene(input.as<JsonObjectConst>(),scene)); }

 // All original glyph pixels survive the lossless payload repacking.
 { std::ifstream goldenFile("tests/native-font-digest.json");JsonDocument golden;assert(!deserializeJson(golden,goldenFile));Sha256 hash;unsigned count=0;for(const auto&g:nm::NATIVE_GLYPHS){uint8_t bitmap[512];nm::FontMetrics metrics;assert(nm::nativeGlyphBitmap(g.code,g.fontSize(),bitmap,sizeof bitmap,metrics));size_t bytes=((g.w+7)/8)*g.h;hash.update(bitmap,bytes);assert(metrics.w==g.w&&metrics.h==g.h&&metrics.advance==g.advance&&metrics.x==g.x&&metrics.y==g.y);++count;}char digest[65];hash.finish(digest);assert(count==golden["glyphs"].as<unsigned>()&&golden["bitmap_sha256"]==digest);}
 // Native compositions may install additional image decoders before the UI starts.
 assert(bindUiMemory(core.resources()));lv_init();for(unsigned i=0;i<2;++i){auto decoder=lv_image_decoder_create();assert(decoder);lv_image_decoder_set_info_cb(decoder,[](lv_image_decoder_t*,lv_image_decoder_dsc_t*,lv_image_header_t*){return LV_RESULT_INVALID;});}

 assert(prepareUi("/system/ui/home.json"));assert(uiDecoderCount>=5);std::ofstream image("build/os-ui-default.native",std::ios::binary);nm::Stripe stripe;
 for(int x=0;x<480;x+=16){assert(renderUiBand(x,stripe));image.write((const char*)stripe.bytes,sizeof(stripe.bytes));}image.close();
 const char* changed=R"({"version":"0.1.1","assets":[{"type":"viewScreen","id":"new","style":{"bgColor":"#ffff00"},"children":[{"type":"label","labelProps":{"text":"UPDATED"},"style":{"fontSize":"40sp","textColor":"#ff0000"}}]}]})";
 size_t wrote;assert(!files.write("/updated.json",0,(const uint8_t*)changed,strlen(changed),true,wrote));assert(prepareUi("/storage/ram/updated.json"));assert(renderUiBand(200,stripe));assert(stripe.bytes[100]==0x22);
 JsonDocument a,r;a["path"]="/system/ui/home.json";assert(!uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));a["path"]="/storage/ram/updated.json";assert(!uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(uiDepth==1);a.clear();assert(!uiHandler("refresh",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(uiDepth==1);assert(!uiHandler("back",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(!strcmp(uiCurrent,"/system/ui/home.json"));
 // Per-presentation values and rotation reach the actual rasterizer. Invalid
 // values cannot mutate navigation, and hidden battery UI is actually omitted.
 std::ifstream templateInput("tests/os-ui/ui-model.json");std::string templateText((std::istreambuf_iterator<char>(templateInput)),{});assert(!files.write("/template.json",0,(const uint8_t*)templateText.data(),templateText.size(),true,wrote));
 a.clear();a["path"]="/storage/ram/template.json";a["values"]["title"]="New title";a["rotation"]=90;a["hidden"].to<JsonArray>().add("indicator");
 assert(!uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(uiDocument(presentationState.active())->view.rotation==90);
 assert(prepareUi("/storage/ram/template.json",uiDocument(presentationState.active())->view));assert(lv_display_get_horizontal_resolution(uiDisplay)==720);assert(lv_display_get_vertical_resolution(uiDisplay)==480);
 assert(!strcmp(lv_label_get_text(lv_obj_get_child(uiRoot,0)),"New title"));assert(lv_obj_get_child_count(uiRoot)==1);
 {auto active=presentationState.active();auto before=uiQueueCalls;auto root=uiRoot;std::string rendered=lv_label_get_text(lv_obj_get_child(uiRoot,0));std::string values;serializeJson(uiValues,values);JsonDocument check,result;check["path"]="/storage/ram/updated.json";check["rotation"]=180;assert(!uiHandler("validate",check.as<JsonObjectConst>(),result.to<JsonObject>()));assert(result["validated"]==true&&result["presented"]==false);assert(uiRoot==root&&presentationState.active()==active&&uiQueueCalls==before&&lv_display_get_horizontal_resolution(uiDisplay)==720);std::string after;serializeJson(uiValues,after);assert(values==after&&rendered==lv_label_get_text(lv_obj_get_child(uiRoot,0)));}

 {const char*future=R"({"version":"0.1.1","assets":[{"type":"viewScreen","style":{"font":"registered-by-app-later"}}]})";assert(!files.write("/future-font.json",0,(const uint8_t*)future,strlen(future),true,wrote));JsonDocument check,result;check["path"]="/storage/ram/future-font.json";check["schema_only"]=true;auto active=presentationState.active();auto queued=uiQueueCalls;assert(!uiHandler("validate",check.as<JsonObjectConst>(),result.to<JsonObject>())&&result["render_ready"]==false);check["schema_only"]=false;assert(!strcmp(uiHandler("validate",check.as<JsonObjectConst>(),result.to<JsonObject>()),"font_or_size_not_registered"));assert(presentationState.active()==active&&uiQueueCalls==queued);}
 for(int x=0;x<480;x+=16)assert(renderUiBand(x,stripe));a["rotation"]=45;assert(uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(uiDocument(presentationState.active())->view.rotation==90);
 assert(prepareUi("/storage/ram/updated.json"));assert(renderUiBand(200,stripe));
 // Background model invalidation never navigates, regardless of caller identity.
 uint32_t first=presentationState.active();a.clear();a["path"]="/storage/ram/updated.json";assert(!uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));uint32_t second=presentationState.active();unsigned submitted=uiQueueCalls;
 a.clear();a["view_id"]=first;a["values"]["title"]="Background clock tick";assert(!uiHandler("update",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(presentationState.active()==second&&uiQueueCalls==submitted&&r["foreground"]==false);
 a.clear();assert(!uiHandler("back",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(presentationState.active()==first&&uiQueueCalls==submitted+1);assert(uiDocument(first)->view.rotation==90);assert(presentationState.current(lastUiStamp));
 assert(prepareUi("/storage/ram/updated.json"));assert(renderUiBand(200,stripe));
 // Failed candidates preserve the current rendered tree.
 const char*bad=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"unsupported"}]}]})";
 assert(!files.write("/bad.json",0,(const uint8_t*)bad,strlen(bad),true,wrote));assert(!prepareUi("/storage/ram/bad.json"));assert(renderUiBand(200,stripe));assert(stripe.bytes[100]==0x22);
 // A full-screen uncompressed asset lives in the host fixture store, standing in for SD.
 // The renderer must read bounded regions, never the complete 691,212-byte file.
 files.budget(1100000);std::vector<uint8_t> bin(12+480*720*2,0);bin[0]=0x19;bin[1]=0x12;bin[4]=0xe0;bin[5]=1;bin[6]=0xd0;bin[7]=2;bin[8]=0xc0;bin[9]=3;
 for(size_t i=12;i<bin.size();i+=2){bin[i]=0;bin[i+1]=0xf8;}
 assert(!files.mkdir("/assets"));assert(!files.write("/assets/picture.bin",0,bin.data(),bin.size(),true,wrote));
 const char*large=R"({"version":"0.1.1","assets":[{"type":"viewScreen","style":{"padding":0},"children":[{"type":"image","imageProps":{"src":"assets/picture.bin"}}]}]})";
 assert(!files.write("/large.json",0,(const uint8_t*)large,strlen(large),true,wrote));files.largestRead=files.totalRead=files.readOps=0;assert(prepareUi("/storage/ram/large.json"));for(int x=0;x<480;x+=16){assert(renderUiBand(x,stripe));assert(stripe.bytes[100]==0x33);}assert(files.largestRead<=4096);size_t imageReadOps=files.readOps,imageReadBytes=files.totalRead;
 files.failOnce=true;assert(!renderUiBand(160,stripe));assert(!strcmp(uiError,"spi_busy"));assert(renderUiBand(160,stripe));assert(stripe.bytes[100]==0x33);
 // Exercise the separate alpha plane through the real LVGL binary decoder.
 std::vector<uint8_t> alpha(12+64*64*3,0);alpha[0]=0x19;alpha[1]=0x14;alpha[4]=64;alpha[6]=64;alpha[8]=128;
 for(size_t i=12;i<12+64*64*2;i+=2)alpha[i+1]=0xf8;
 for(size_t i=12+64*64*2;i<alpha.size();++i)alpha[i]=128;
 assert(!files.write("/assets/icon.bin",0,alpha.data(),alpha.size(),true,wrote));
 const char*external=R"({"type":"viewScreen","style":{"padding":0},"children":[{"type":"image","imageProps":{"src":"icon.bin"}}]})";
 assert(!files.write("/assets/screen.json",0,(const uint8_t*)external,strlen(external),true,wrote));
 const char*root=R"({"version":"0.1.1","assets":["assets/screen.json"]})";assert(!files.write("/external.json",0,(const uint8_t*)root,strlen(root),true,wrote));
 assert(prepareUi("/storage/ram/external.json"));assert(renderUiBand(0,stripe));
 // Indexed glyphs remain seekable and leased. Malformed user files fail at
 // the format boundary rather than entering an unchecked outline parser.
 std::vector<uint8_t> fontData(16+95*16+95*7);memcpy(fontData.data(),"NMF1",4);
 auto put=[&](size_t at,uint32_t value,unsigned n){for(unsigned i=0;i<n;++i)fontData[at+i]=value>>(8*i);};
 put(4,95,4);put(8,95*7,4);
 for(unsigned i=0;i<95;++i){size_t at=16+i*16;put(at,32+i,4);fontData[at+4]=22;fontData[at+5]=5;fontData[at+6]=7;fontData[at+8]=2;fontData[at+9]=6;put(at+10,7,2);put(at+12,i*7,4);memset(fontData.data()+16+95*16+i*7,0xA8,7);}
 assert(!files.write("/font.nmf",0,fontData.data(),fontData.size(),true,wrote));
 a.clear();a["id"]="custom";a["file"]="/storage/ram/font.nmf";a["size"]=22;assert(!uiHandler("font_register",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(!vfs.canMutate("/storage/ram/font.nmf"));
 const char*fontUi=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"label","style":{"font":"custom","fontSize":22},"labelProps":{"text":"Runtime font"}}]}]})";
 assert(!files.write("/font-ui.json",0,(const uint8_t*)fontUi,strlen(fontUi),true,wrote));assert(prepareUi("/storage/ram/font-ui.json"));assert(renderUiBand(40,stripe));assert(!strcmp(uiHandler("font_unregister",a.as<JsonObjectConst>(),r.to<JsonObject>()),"font_in_use"));assert(prepareUi("/system/ui/home.json"));assert(!uiHandler("font_unregister",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(vfs.canMutate("/storage/ram/font.nmf"));
 // A valid PNG header with missing image data must fail rendering, not commit
 // a blank icon as a successful frame.
 const uint8_t brokenPng[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,1,0,0,0,1,8,6,0,0,0,31,21,196,137};
 assert(!files.write("/broken.png",0,brokenPng,sizeof brokenPng,true,wrote));
 const char*brokenUi=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"image","imageProps":{"src":"broken.png"}}]}]})";
 assert(!files.write("/broken-ui.json",0,(const uint8_t*)brokenUi,strlen(brokenUi),true,wrote));assert(prepareUi("/storage/ram/broken-ui.json"));assert(!renderUiBand(0,stripe));assert(!strcmp(uiError,"image_decode_failed"));assert(prepareUi("/system/ui/home.json"));
 // Decoder admission follows the bytes, including upper-case and unrelated
 // extensions. All three files exceed the same declared decoded-image budget.
 std::ifstream pngInput("tests/os-ui/oversize.png",std::ios::binary);
 std::vector<uint8_t> pngBytes((std::istreambuf_iterator<char>(pngInput)),{});assert(!pngBytes.empty());
 for(auto extension:{"png","PNG","bin"}){
  std::string path=std::string("/oversize.")+extension;
  assert(!files.write(path.c_str(),0,pngBytes.data(),pngBytes.size(),true,wrote));
  std::string page=std::string(R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"image","imageProps":{"src":"oversize.)")+extension+R"("}}]}]})";
  assert(!files.write("/oversize-ui.json",0,(const uint8_t*)page.data(),page.size(),true,wrote));
  assert(!prepareUi("/storage/ram/oversize-ui.json"));assert(!strcmp(uiError,"large_png_requires_streamable_asset"));
 }
 assert(renderUiBand(0,stripe));
 JsonDocument pngPolicy,pngReply;deserializeJson(pngPolicy,R"({"changes":[{"resource":"/sys/ui","settings":{"png_decode_budget_bytes":65536}}]})");
 assert(!policies.apply(pngPolicy.as<JsonObjectConst>(),pngReply.to<JsonObject>()));assert(prepareUi("/storage/ram/oversize-ui.json"));if(!renderUiBand(0,stripe)){std::cerr<<"PNG error: "<<uiError<<std::endl;abort();}
 pngPolicy["changes"][0]["settings"]["png_decode_budget_bytes"]=16384;assert(!policies.apply(pngPolicy.as<JsonObjectConst>(),pngReply.to<JsonObject>()));assert(prepareUi("/system/ui/home.json"));
 // Registration does not trust glyph offsets in a runtime-provided font.
 put(16+('?' -32)*16+12,0xffffffff,4);assert(!files.write("/bad-font.nmf",0,fontData.data(),fontData.size(),true,wrote));a.clear();a["id"]="invalid";a["file"]="/storage/ram/bad-font.nmf";a["size"]=22;assert(!strcmp(uiHandler("font_register",a.as<JsonObjectConst>(),r.to<JsonObject>()),"font_glyph_range"));assert(vfs.canMutate("/storage/ram/bad-font.nmf"));
 // Inject a genuine allocator failure at each retained-tree allocation. The
 // current tree survives every rejected candidate and can still be rendered.
 core.resources().heap(ResourceBudget::Region::Internal,{nullptr,nullptr,nullptr,uiFaultResize});
 size_t allocationBoundaries=0;
 for(size_t pass=0;pass==0||pass<=allocationBoundaries;++pass){
  uiFailAt=SIZE_MAX;assert(prepareUi("/storage/ram/updated.json"));auto previousRoot=uiRoot;
  uiAllocCalls=0;uiFailAt=pass?pass-1:SIZE_MAX;bool accepted=prepareUi("/system/ui/home.json");
  if(!pass)allocationBoundaries=uiAllocCalls;uiFailAt=SIZE_MAX;
  if(!accepted){assert(uiRoot==previousRoot);assert(!strcmp(uiError,"ui_memory_budget"));}
  assert(renderUiBand(0,stripe));
 }
 std::cout<<"UI retained-tree allocation boundaries "<<allocationBoundaries<<" passed\n";
 JsonDocument resources;resources["memory_limit_bytes"]=uint32_t(core.resources().promised()+1);assert(!core.resources().configure(resources.as<JsonObjectConst>()));assert(!prepareUi("/system/ui/home.json"));assert(!strcmp(uiError,"ui_memory_budget"));resources["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(resources.as<JsonObjectConst>()));assert(prepareUi("/system/ui/home.json"));
 for(;;){JsonDocument state;presentationState.describe(state.to<JsonObject>());if(!state["views"].size())break;uint32_t id=state["views"][0]["id"];unsigned slot=presentationState.slot(id);assert(!presentationState.close(id,0));freeUiDocument(slot);}resetUiRenderer();assert(core.resources().promised()==0);
 // The real factory entrypoint uses the exact runtime/GUI loader used by a user
 // replacement. No special native boot path or system-owned button bindings.
 core.add({"/sys/ui","read,app_load,open,update,refresh,back,close,font_register,font_unregister,fonts", "",uiHandler,uiAsyncDescription});
 core.add({"/dev/buttons","read","",[](const char*,JsonObjectConst,JsonObject)->const char*{return nullptr;}});
 core.add({"/dev/mic","start,stop","",[](const char*m,JsonObjectConst,JsonObject)->const char*{if(!strcmp(m,"start"))++micStarts;return nullptr;}});
 core.add({"/sys/rules","write","",[](const char*,JsonObjectConst a,JsonObject o){return core.setRule(a,o);}});
 const char*factoryManifest=R"({"format":"notes-app/1","entry":"updated.json","bindings":[{"source":"/dev/buttons","event":"press","where":{"button":"B"},"target":"/dev/mic","method":"start","cooldown_ms":0}]})";
 assert(!files.write("/factory-app.json",0,(const uint8_t*)factoryManifest,strlen(factoryManifest),true,wrote));
 ScriptHost runtime(core,vfs);a.clear();a["source"]="function on_start() local a=notes.call('/sys/ui','app_load',{file='/storage/ram/factory-app.json'}); assert(a.ok,a.error); local r=notes.call('/sys/ui','open',{path=a.result.entry}); assert(r.ok,r.error) end";
 if(auto e=runtime.invoke("load",a.as<JsonObjectConst>(),r.to<JsonObject>())){std::cerr<<e<<" ";serializeJson(r,std::cerr);assert(false);}
 assert(uiRuleCount==1&&uiAppOwner!=0&&presentationState.active());unsigned factoryOwner=uiAppOwner;
 uint32_t cursor=core.sequence();a.clear();a["button"]="B";core.emit("/dev/buttons","press",a.as<JsonObjectConst>());core.dispatch(cursor);assert(micStarts==1);
 // A bad replacement must preserve both working bindings and the current view.
 const char*badApp=R"({"format":"notes-app/1","entry":"bad.json","bindings":[]})";assert(!files.write("/bad-app.json",0,(const uint8_t*)badApp,strlen(badApp),true,wrote));
 a.clear();a["file"]="/storage/ram/bad-app.json";unsigned active=presentationState.active();
 assert(core.invoke("/sys/ui","app_load",a.as<JsonObjectConst>(),r.to<JsonObject>(),factoryOwner));assert(uiRuleCount==1&&presentationState.active()==active);
 a.clear();a["source"]=R"lua(function on_start()
  assert(notes.call('/sys/rules','write',{source='/dev/buttons',event='press',where={button='A'},target='/dev/mic',method='start',cooldown_ms=0}).ok)
  assert(notes.call('/sys/ui','open',{path='/storage/ram/updated.json'}).ok)
 end)lua";
 assert(!runtime.invoke("load",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(uiRuleCount==0&&uiAppOwner==0);
 assert(!strcmp(uiCurrent,"/storage/ram/updated.json"));
 a.clear();a["button"]="B";core.emit("/dev/buttons","press",a.as<JsonObjectConst>());core.dispatch(cursor);assert(micStarts==1);
 a["button"]="A";core.emit("/dev/buttons","press",a.as<JsonObjectConst>());core.dispatch(cursor);assert(micStarts==2);
 a.clear();assert(!runtime.invoke("stop",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(!presentationState.active());assert(core.resources().promised()==0);
 core.listRules(r["rules"].to<JsonArray>());assert(r["rules"].size()==0);
 // Completion callbacks also settle UI revisions that never obtained a job:
 // latest model replacement and close must not leave subscribers waiting forever.
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return core.describe(a["path"]|"",o);}});
 callbackRuntime=&runtime;core.setEventObserver([](const Event&e){return callbackRuntime->event(e);});deferUi=true;cursor=core.sequence();unsigned callbacksBefore=micStarts;
 a.clear();a["memory_bytes"]=65536;a["source"]=R"lua(function on_start()
  local first=notes.request('/sys/ui','open',{path='/storage/ram/updated.json'},function(e)
   assert(e.event=='failed' and e.data.error=='view_revision_replaced' and e.data.completion.source=='not_admitted')
   notes.call('/dev/mic','start',{})
  end);assert(first.ok)
  local second=notes.request('/sys/ui','update',{view_id=first.result.view_id},function(e)
   assert(e.event=='failed' and e.data.error=='view_closed');notes.call('/dev/mic','start',{})
  end);assert(second.ok)
  assert(notes.call('/sys/ui','close',{view_id=first.result.view_id}).ok)
 end)lua";
 if(auto error=runtime.invoke("load",a.as<JsonObjectConst>(),r.to<JsonObject>())){std::cerr<<error<<" ";serializeJson(r,std::cerr);assert(false);}assert(micStarts==callbacksBefore);
 core.dispatch(cursor);assert(micStarts==callbacksBefore+2);a.clear();assert(!runtime.invoke("read",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["callbacks"]==0&&!r["fault"].as<bool>());
 assert(!runtime.invoke("stop",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(core.resources().promised()==0);core.setEventObserver(nullptr);callbackRuntime=nullptr;deferUi=false;
 // Font registrations share application ownership. An in-flight renderer
 // retains an old binding until safe disposal; a new app may reuse its name.
 { auto old=core.acquireOwner();a.clear();a["id"]="custom";a["file"]="/storage/ram/font.nmf";a["size"]=22;assert(!core.invoke("/sys/ui","font_register",a.as<JsonObjectConst>(),r.to<JsonObject>(),old));
   a.clear();a["path"]="/storage/ram/font-ui.json";assert(!core.invoke("/sys/ui","open",a.as<JsonObjectConst>(),r.to<JsonObject>(),old));auto oldView=r["view_id"].as<uint32_t>();assert(prepareUi("/storage/ram/font-ui.json",uiDocument(oldView)->view));uiLeaseOwner=0xfffffff1;core.releaseOwner(old);assert(!vfs.canMutate("/storage/ram/font.nmf"));
   auto next=core.acquireOwner();a.clear();a["id"]="custom";a["file"]="/storage/ram/font.nmf";a["size"]=22;assert(!core.invoke("/sys/ui","font_register",a.as<JsonObjectConst>(),r.to<JsonObject>(),next));a.clear();a["path"]="/storage/ram/font-ui.json";assert(!core.invoke("/sys/ui","open",a.as<JsonObjectConst>(),r.to<JsonObject>(),next));auto nextView=r["view_id"].as<uint32_t>();assert(prepareUi("/storage/ram/font-ui.json",uiDocument(nextView)->view));for(const auto&f:fontFiles)assert(!f.font||f.owner!=old);assert(renderUiBand(0,stripe));
   core.releaseOwner(next);assert(!vfs.canMutate("/storage/ram/font.nmf"));vfs.release(uiLeaseOwner);uiLeaseOwner=0;resetUiRenderer();assert(vfs.canMutate("/storage/ram/font.nmf"));assert(core.resources().promised()==0);
 }
 // Pointer input addresses rendered element IDs rather than app-specific
 // coordinate tables, including rotation and a not-yet-presented replacement.
 const char*interactive=R"({"version":"0.1.1","assets":[{"type":"viewScreen","id":"canvas","style":{"padding":0},"children":[{"type":"button","id":"launch","placement":{"x":10,"y":10,"width":100,"height":80},"style":{"padding":0},"children":[{"type":"label","id":"caption","labelProps":{"text":"Open"}}]}]}]})";
 assert(!files.write("/hit.json",0,(const uint8_t*)interactive,strlen(interactive),true,wrote));a.clear();a["path"]="/storage/ram/hit.json";a["history"]="replace";assert(!uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));auto hitView=r["view_id"].as<uint32_t>();assert(prepareUi("/storage/ram/hit.json",uiDocument(hitView)->view));
 a.clear();a["x"]=20;a["y"]=20;assert(!strcmp(uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()),"presentation_not_ready"));presentationState.completed(presentationState.stamp(hitView));commitUiGeometry(presentationState.stamp(hitView));assert(!uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["id"]=="caption"&&r["ids"][1]=="launch");
  // A new model can be admitted while input still uses immutable geometry
 // from the last completed frame, until the native adapter commits its successor.
 auto shownRevision=uiVisibleStamp.revision;a.clear();a["view_id"]=hitView;a["values"]["caption"]="Changed";assert(!uiHandler("update",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/storage/ram/hit.json",uiDocument(hitView)->view));a.clear();a["x"]=20;a["y"]=20;assert(!uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["ids"][1]=="launch"&&r["revision"]==shownRevision);presentationState.completed(presentationState.stamp(hitView));commitUiGeometry(presentationState.stamp(hitView));
 a.clear();a["view_id"]=hitView;a["rotation"]=90;assert(!uiHandler("update",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/storage/ram/hit.json",uiDocument(hitView)->view));presentationState.completed(presentationState.stamp(hitView));commitUiGeometry(presentationState.stamp(hitView));a.clear();a["x"]=boardpack::DISPLAY_WIDTH-21;a["y"]=20;assert(!uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["ids"][1]=="launch"&&r["x"]==20&&r["y"]==20);a["x"]=-1;assert(!strcmp(uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()),"pointer_coordinate_range"));
 a.clear();a["view_id"]=hitView;assert(!uiHandler("close",a.as<JsonObjectConst>(),r.to<JsonObject>()));resetUiRenderer();assert(core.resources().promised()==0);
 // A complete 8 MiB font remains a filesystem resource. Previously absent
 // Chinese characters and non-preset sizes use its outlines, not compiled text.
 a.clear();assert(!uiHandler("capabilities",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["external_fonts"]=="truetype-file,nmf1-indexed-1bpp"&&r["scalable_font_sizes"]["minimum"]==8&&r["scalable_font_sizes"]["maximum"]==96);
 vfs.add(completeFonts);const char*fullFontUi=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"label","style":{"font":"/fonts/complete.ttf","fontSize":19},"labelProps":{"text":"实验触摸录音 阈值蝴蝶龘"}},{"type":"label","style":{"font":"/fonts/complete.ttf","fontSize":23},"labelProps":{"text":"实验触摸录音 阈值蝴蝶龘"}},{"type":"label","style":{"font":"/fonts/complete.ttf","fontSize":29},"labelProps":{"text":"实验触摸录音 阈值蝴蝶龘"}}]}]})";
 assert(!nm::localTextSupported("阈值蝴蝶龘",24));assert(!files.write("/complete-font-ui.json",0,reinterpret_cast<const uint8_t*>(fullFontUi),strlen(fullFontUi),true,wrote));
 assert(prepareUi("/storage/ram/complete-font-ui.json"));for(int x=0;x<480;x+=16)assert(renderUiBand(x,stripe));std::cerr<<"Full font: missing="<<uiMissingGlyphs<<" largest="<<completeFonts.largestRead<<" read="<<completeFonts.readBytes<<" live="<<uiMemoryUsed()<<"\n";assert(uiMissingGlyphs==0&&completeFonts.largestRead<=4096&&completeFonts.readBytes<1048576);assert(!vfs.canMutate("/fonts/complete.ttf"));
 JsonDocument fontPolicy;fontPolicy["changes"][0]["resource"]="/sys/ui";fontPolicy["changes"][0]["settings"]["font_file"]="/fonts/complete.ttf";assert(!policies.apply(fontPolicy.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/system/ui/home.json"));assert(renderUiBand(0,stripe)&&uiMissingGlyphs==0);
 a.clear();a["id"]="whole-family";a["file"]="/fonts/complete.ttf";assert(!uiHandler("font_register",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(!strcmp(uiHandler("font_unregister",a.as<JsonObjectConst>(),r.to<JsonObject>()),"font_in_use"));
 {JsonDocument bench;bench["version"]="0.1.1";auto screen=bench["assets"].to<JsonArray>().add<JsonObject>();screen["type"]="viewScreen";auto label=screen["children"].to<JsonArray>().add<JsonObject>();label["type"]="label";label["style"]["fontSize"]=16;std::string text;for(char c=32;c<127;++c)text+=c;label["labelProps"]["text"]=text;std::string encoded;serializeJson(bench,encoded);assert(!files.write("/cache-benchmark.json",0,(const uint8_t*)encoded.data(),encoded.size(),true,wrote));
  auto redraw=[&](unsigned cache){fontPolicy["changes"][0]["settings"]["font_cache_glyphs"]=cache;assert(!policies.apply(fontPolicy.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/storage/ram/cache-benchmark.json"));UiSink sink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};assert(renderUiRegion({0,0,479,719},sink));auto before=completeFonts.readBytes;assert(renderUiRegion({0,0,479,719},sink));return completeFonts.readBytes-before;};
  auto small=redraw(32),large=redraw(128);std::cerr<<"Font warm-render source reads: cache32="<<small<<" cache128="<<large<<"\n";assert(small>large&&uiMissingGlyphs==0);fontPolicy["changes"][0]["settings"]["font_cache_glyphs"]=32;
 }
 fontPolicy["changes"][0]["settings"]["font_file"]="";assert(!policies.apply(fontPolicy.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/system/ui/home.json"));assert(!uiHandler("font_unregister",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(vfs.canMutate("/fonts/complete.ttf"));
 completeFonts.failOnce=true;auto kept=uiRoot;assert(!prepareUi("/storage/ram/complete-font-ui.json"));assert(uiRoot==kept&&!strcmp(uiError,"font_fixture_io")&&vfs.canMutate("/fonts/complete.ttf"));
 size_t fontBoundaries=0;for(size_t pass=0;pass==0||pass<=fontBoundaries;++pass){uiFailAt=SIZE_MAX;assert(prepareUi("/system/ui/home.json"));kept=uiRoot;uiAllocCalls=0;uiFailAt=pass?pass-1:SIZE_MAX;bool accepted=prepareUi("/storage/ram/complete-font-ui.json");if(!pass)fontBoundaries=uiAllocCalls;uiFailAt=SIZE_MAX;if(!accepted)assert(uiRoot==kept);assert(prepareUi("/system/ui/home.json"));assert(vfs.canMutate("/fonts/complete.ttf"));}
 assert(prepareUi("/storage/ram/complete-font-ui.json"));JsonDocument fontMemory;fontMemory["memory_limit_bytes"]=uint32_t(core.resources().promised()+1);assert(!core.resources().configure(fontMemory.as<JsonObjectConst>()));assert(!renderUiBand(32,stripe));assert(!strcmp(uiError,"ui_memory_budget"));fontMemory["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(fontMemory.as<JsonObjectConst>()));assert(renderUiBand(32,stripe));
 resetUiRenderer();assert(core.resources().promised()==0);std::cout<<"Complete font streaming, arbitrary sizes, default selection, I/O rejection and font allocation rollback passed\n";
 std::cout<<"Full-screen image bytes="<<bin.size()<<", largest provider read="<<files.largestRead<<", image reads="<<imageReadBytes<<", image read calls="<<imageReadOps<<"\n";
 // An implicit system replacement must not inherit a foreground VM's view.
 // Repeated owner/system replacements release document slots and allocations.
 {for(unsigned i=0;i<PresentationState::Capacity;++i){auto id=presentationState.at(i).id;if(id)assert(!presentationState.close(id,0));}
  auto owner=core.acquireOwner();uint32_t prior=0;
  for(unsigned i=0;i<24;++i){a.clear();a["path"]="/storage/ram/updated.json";a["history"]="replace";assert(!core.invoke("/sys/ui","open",a.as<JsonObjectConst>(),r.to<JsonObject>(),i%2?0:owner));auto id=r["view_id"].as<uint32_t>();assert(id!=prior);if(prior)assert(!presentationState.get(prior)&&!uiDocument(prior));prior=id;}
  a.clear();a["view_id"]=prior;a["values"]["title"]="Explicit system update";assert(!core.invoke("/sys/ui","update",a.as<JsonObjectConst>(),r.to<JsonObject>(),0));assert(presentationState.active()==prior);
  core.releaseOwner(owner);assert(presentationState.get(prior));assert(!presentationState.close(prior,0));assert(!presentationState.active());for(auto d:uiDocuments)assert(!d);
 }
 std::cout<<"LVGL runtime-loaded UI, native stripe render, VFS hot replacement, refresh/back history and cross-owner replacement passed\n";
}
