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
 core.add({"/sys/ui","read,app_load,open,update,refresh,back,close", "",uiHandler,uiAsyncDescription});
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
 std::cout<<"Full-screen image bytes="<<bin.size()<<", largest provider read="<<files.largestRead<<", image reads="<<imageReadBytes<<", image read calls="<<imageReadOps<<"\n";
 std::cout<<"LVGL runtime-loaded UI, native stripe render, VFS hot replacement, refresh/back history passed\n";
}
