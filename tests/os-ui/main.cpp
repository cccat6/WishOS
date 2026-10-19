#include "UiMemory.h"
#include "drivers/esp32-services/PosixFileProvider.h"
#define LODEPNG_NO_COMPILE_CPP
#include <src/libs/lodepng/lodepng.h>
#include <filesystem>
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
#include "ScriptHost.h"
#include "Sha256.h"
#include "display/NativeFont.h"
#include "Vfs.h"
#include "../fixtures/composition/Defaults.h"
#include "SystemFiles.h"
#include "../fixtures/scene/Content.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
static size_t uiAllocCalls=0,uiFailAt=SIZE_MAX,uiRejectedAllocationSize=0,uiObservedAllocationSize=0;
static void*uiMetadataFaultResize(void*p,size_t n){if(!n){free(p);return nullptr;}uiObservedAllocationSize=n;if(n==uiRejectedAllocationSize)return nullptr;return realloc(p,n);}
static void*uiFaultResize(void*p,size_t n){if(!n){free(p);return nullptr;}if(uiAllocCalls++==uiFailAt)return nullptr;return realloc(p,n);}
uint32_t millis(){static uint32_t t=0;return ++t;}
namespace notesos {
static Core core;static Policies policies(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));static char appScope[48]="system";static Vfs vfs;static SystemFiles systemFiles;class CountedFiles:public MemoryFiles {public:bool failOnce=false,online=true,revisionless=false,reliableContent=false;size_t largestRead=0,totalRead=0,readOps=0,statOps=0;CountedFiles(const char*id="ram",const char*mount="/storage/ram"):MemoryFiles(id,mount,"ram"){}bool available()const override{return online&&MemoryFiles::available();}uint64_t changeRevision()const override{return revisionless?0:MemoryFiles::changeRevision();}uint64_t contentRevision()const override{return reliableContent?changeRevision():MemoryFiles::contentRevision();}const char*stat(const char*p,FileInfo&i)override{++statOps;return available()?MemoryFiles::stat(p,i):"provider_unavailable";}const char*read(const char*p,uint64_t at,uint8_t*b,size_t n,size_t&got)override{if(!available()){got=0;return "provider_unavailable";}if(failOnce){failOnce=false;got=0;return "spi_busy";}++readOps;largestRead=std::max(largestRead,n);auto e=MemoryFiles::read(p,at,b,n,got);totalRead+=got;return e;}};
static CountedFiles files;static CountedFiles replacementImages("replacement-images","/replacement-images");
static bool removableImagesMounted=true;static char removableImagesRoot[]="/tmp/wishos-ui-media-XXXXXX";static PosixFileProvider removableImages("removable-images","/removable-images","sd",true,removableImagesMounted,removableImagesRoot,"","fat",512);
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
#include "font_family_scenarios.inc"
#include "../fixtures/RasterSink.inc"
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
 // Real streamable RGB565 pixels exercise large row reads, partial raster
 // regions and odd image geometry independently of the source window size.
 {constexpr unsigned width=319,height=37,left=7,top=5;std::vector<uint8_t> pixels(12+width*height*2);pixels[0]=0x19;pixels[1]=0x12;pixels[4]=width&255;pixels[5]=width>>8;pixels[6]=height;pixels[8]=(width*2)&255;pixels[9]=(width*2)>>8;
  auto color=[](unsigned x,unsigned y){return uint16_t(((x*3+y)&31)<<11|((x+y*7)&63)<<5|((x*11+y*3)&31));};for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){auto c=color(x,y);size_t at=12+(y*width+x)*2;pixels[at]=c;pixels[at+1]=c>>8;}
  assert(!files.write("/assets/rows.bin",0,pixels.data(),pixels.size(),true,wrote));const char*page=R"({"version":"0.1.1","assets":[{"type":"viewScreen","style":{"padding":0,"bgColor":"#ffffff"},"children":[{"type":"image","placement":{"x":7,"y":5,"width":319,"height":37},"imageProps":{"src":"assets/rows.bin"}}]}]})";assert(!files.write("/rows.json",0,reinterpret_cast<const uint8_t*>(page),strlen(page),true,wrote));assert(prepareUi("/storage/ram/rows.json"));
  std::vector<uint16_t> full(boardpack::DISPLAY_WIDTH*64),partitioned(full.size());auto sinkFor=[](std::vector<uint16_t>&target){return UiSink{&target,[](void*context,const lv_area_t&area,const uint16_t*data){auto&out=*static_cast<std::vector<uint16_t>*>(context);for(int y=area.y1;y<=area.y2;++y)memcpy(out.data()+y*boardpack::DISPLAY_WIDTH+area.x1,data+(y-area.y1)*(area.x2-area.x1+1),(area.x2-area.x1+1)*sizeof(uint16_t));}};};
  auto sink=sinkFor(full);files.readOps=0;assert(renderUiRegion({0,0,boardpack::DISPLAY_WIDTH-1,63},sink,true));auto readOps=files.readOps;for(unsigned y=0;y<64;++y)for(unsigned x=0;x<boardpack::DISPLAY_WIDTH;++x)assert(full[y*boardpack::DISPLAY_WIDTH+x]==(x>=left&&x<left+width&&y>=top&&y<top+height?color(x-left,y-top):uint16_t(0xffff)));
  sink=sinkFor(partitioned);for(int y=0;y<64;y+=9)assert(renderUiRegion({0,y,boardpack::DISPLAY_WIDTH-1,std::min(y+8,63)},sink,true));assert(full==partitioned);std::cout<<"Streaming RGB565 odd-row fixture: source_reads="<<readOps<<", payload_bytes="<<pixels.size()<<"; full/partial pixels exact\n";assert(!files.remove("/assets/rows.bin")&&!files.remove("/rows.json"));
 }
 // Exercise the separate alpha plane through the real LVGL binary decoder.
 std::vector<uint8_t> alpha(12+64*64*3,0);alpha[0]=0x19;alpha[1]=0x14;alpha[4]=64;alpha[6]=64;alpha[8]=128;
 for(size_t i=12;i<12+64*64*2;i+=2)alpha[i+1]=0xf8;
 for(size_t i=12+64*64*2;i<alpha.size();++i)alpha[i]=128;
 assert(!files.write("/assets/icon.bin",0,alpha.data(),alpha.size(),true,wrote));
 const char*external=R"({"type":"viewScreen","style":{"padding":0},"children":[{"type":"image","imageProps":{"src":"icon.bin"}}]})";
 assert(!files.write("/assets/screen.json",0,(const uint8_t*)external,strlen(external),true,wrote));
 const char*root=R"({"version":"0.1.1","assets":["assets/screen.json"]})";assert(!files.write("/external.json",0,(const uint8_t*)root,strlen(root),true,wrote));
 assert(prepareUi("/storage/ram/external.json"));assert(renderUiBand(0,stripe));
 // A compressed source spanning many output bands is decoded once when
 // admitted to the cache. A file replacement must invalidate that decode.
 {std::ifstream png("hardware/agent-os/runtime/lckfb-suite/assets/brand/logo.png",std::ios::binary);std::vector<uint8_t>data((std::istreambuf_iterator<char>(png)),{});assert(!files.write("/cached.png",0,data.data(),data.size(),true,wrote));
  const char*page=R"({"version":"0.1.1","assets":[{"type":"viewScreen","style":{"padding":0},"children":[{"type":"image","placement":{"x":0,"y":0,"width":480,"height":720},"imageProps":{"src":"cached.png","fit":"contain"}}]}]})";assert(!files.write("/cached-ui.json",0,(const uint8_t*)page,strlen(page),true,wrote));
  JsonDocument policy,result;policy["changes"][0]["resource"]="/sys/ui";auto settings=policy["changes"][0]["settings"].to<JsonObject>();UiSink sink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};
  auto draw=[&](unsigned cache){settings["image_cache_bytes"]=cache;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));assert(prepareUi("/storage/ram/cached-ui.json"));files.readOps=0;assert(renderUiRegion({0,0,479,719},sink));return files.readOps;};
  auto uncached=draw(0),cached=draw(65536);std::cerr<<"PNG provider reads: uncached="<<uncached<<" cached="<<cached<<"\n";assert(cached<uncached);auto warm=draw(65536);std::cerr<<"PNG next-frame reads: first="<<cached<<" warm="<<warm<<"\n";assert(warm<cached);
  // A modest shared-budget adjustment preserves decoded residents that fit.
  configureUiImageCache(65000);files.readOps=0;assert(renderUiRegion({0,0,479,719},sink));assert(files.readOps==0);configureUiImageCache(65536);
  // A positive metadata hit avoids source I/O, while every call still acquires
  // its current read lease and applies the current decoded-PNG budget.
  JsonDocument node;node["imageProps"]["src"]="/storage/ram/cached.png";auto image=lv_image_create(uiRoot);assert(image);
  auto configure=[&](){return configureUiImage(node.as<JsonObjectConst>(),image,JsonObjectConst());};
  clearUiImageMetadata();lv_image_cache_drop(nullptr);files.statOps=files.readOps=0;assert(configure());assert(files.statOps&&files.readOps);
  files.statOps=files.readOps=0;assert(configure());assert(files.statOps==0&&files.readOps==0);assert(uiImageMetadataBytes&&uiImageMetadataBytes+uiAppliedDecodedImageCache==65536);
  {Vfs::Access writer(vfs,true);assert(!writer.add("/storage/ram/cached.png"));files.statOps=files.readOps=0;auto previousSource=lv_image_get_src(image);assert(!configure()&&!strcmp(uiError,"asset_lease_capacity"));assert(!files.statOps&&!files.readOps&&lv_image_get_src(image)==previousSource);}assert(configure());
  auto previousPng=policies.get<uint32_t>("/sys/ui","png_decode_budget_bytes",16384);settings["png_decode_budget_bytes"]=1024;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));files.statOps=files.readOps=0;assert(!configure()&&!strcmp(uiError,"large_png_requires_streamable_asset"));assert(!files.statOps&&!files.readOps);settings["png_decode_budget_bytes"]=previousPng;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));assert(configure());
  // Unavailable media and providers with no revision token never reuse facts.
  files.online=false;assert(!configure()&&!strcmp(uiError,"provider_unavailable"));files.online=true;files.statOps=files.readOps=0;assert(configure());assert(files.statOps&&files.readOps);
  files.revisionless=true;assert(configure());files.statOps=files.readOps=0;assert(configure());assert(files.statOps&&files.readOps);files.revisionless=false;assert(configure());
  // A removable adapter may positively promise a reliable content epoch.
  // Without it new admissions reread; with it the ordinary warm cache works.
  files.removable=true;assert(configure());files.statOps=files.readOps=0;assert(configure()&&files.statOps&&files.readOps);
  files.reliableContent=true;assert(configure());files.statOps=files.readOps=0;assert(configure()&&!files.statOps&&!files.readOps);files.removable=false;files.reliableContent=false;
  // Metadata and decoded pixels share one budget; lowering it immediately
  // retires metadata, without turning a cache refusal into an image refusal.
  configureUiImageCache(1);assert(!uiImageMetadataBytes&&uiAppliedDecodedImageCache==1);assert(configure());assert(!uiImageMetadataBytes);configureUiImageCache(65536);assert(configure());
  // A metadata allocation failure alone leaves valid rendering available.
  clearUiImageMetadata();lv_image_cache_drop(nullptr);core.resources().heap(ResourceBudget::Region::Internal,{nullptr,nullptr,nullptr,uiMetadataFaultResize});auto probe=core.resources().allocate(sizeof(UiImageMetadata));assert(probe);auto requestSize=uiObservedAllocationSize;core.resources().freeAllocation(probe);uiRejectedAllocationSize=requestSize;clearUiMemoryFailure();assert(configure());assert(!uiMemoryFailed()&&!uiImageMetadataBytes);uiRejectedAllocationSize=0;core.resources().heap(ResourceBudget::Region::Internal,{});assert(configure());assert(uiImageMetadataBytes);
  // A new provider can resolve the same absolute name. Neither the OS facts
  // cache nor LVGL's path-only header cache may conceal that replacement.
  assert(vfs.add(replacementImages));assert(!replacementImages.write("/cached.png",0,data.data(),data.size(),true,wrote));files.mount="/retired-images";replacementImages.mount="/storage/ram";replacementImages.statOps=replacementImages.readOps=0;assert(configure());assert(replacementImages.statOps&&replacementImages.readOps);files.mount="/storage/ram";replacementImages.mount="/replacement-images";files.statOps=files.readOps=0;assert(configure());assert(files.statOps&&files.readOps);
  // A removable provider's internal write counter does not observe external
  // replacement. A new UI admission must read header/size despite warm caches.
  {assert(mkdtemp(removableImagesRoot));assert(vfs.add(removableImages));auto native=std::string(removableImagesRoot)+"/image.png";
   auto replace=[&](const uint8_t*bytes,size_t size){std::ofstream out(native,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(bytes),size);assert(out.good());};
   replace(data.data(),data.size());node["imageProps"]["src"]="/removable-images/image.png";assert(configure());assert(configure());
   lv_image_decoder_dsc_t decoded{};assert(lv_image_decoder_open(&decoded,"V:/removable-images/image.png",nullptr)==LV_RESULT_OK);lv_image_decoder_close(&decoded);assert(lv_cache_get_size(LV_GLOBAL_DEFAULT()->img_cache,nullptr)>0);
   std::vector<uint8_t>pixels(128*128*4,255);uint8_t*large=nullptr;size_t largeBytes=0;assert(lodepng_encode32(&large,&largeBytes,pixels.data(),128,128)==0);auto revision=removableImages.changeRevision();replace(large,largeBytes);lv_free(large);assert(removableImages.changeRevision()==revision&&removableImages.available());
   bool accepted=configure();std::cerr<<"Removable external PNG replacement admitted="<<accepted<<" error="<<uiError<<"\n";assert(!accepted&&!strcmp(uiError,"large_png_requires_streamable_asset"));
   replace(data.data(),data.size());assert(configure());auto oversized=data;oversized.resize(previousPng+1);replace(oversized.data(),oversized.size());assert(!configure()&&!strcmp(uiError,"large_png_requires_streamable_asset"));
   assert(std::filesystem::remove(native));assert(!configure()&&!strcmp(uiError,"not_found"));removableImagesMounted=false;std::filesystem::remove_all(removableImagesRoot);node["imageProps"]["src"]="/storage/ram/cached.png";assert(configure());
  }
  // Encoded-file admission is independently rechecked on a metadata hit.
  lv_image_header_t known{};assert(lv_image_decoder_get_info("V:/storage/ram/cached.png",&known)==LV_RESULT_OK);auto encodedLimit=std::max<uint32_t>(uint32_t(data.size()),uint32_t(known.w)*known.h*4)+64;auto padded=data;padded.resize(encodedLimit+1);assert(!files.write("/cached.png",0,padded.data(),padded.size(),true,wrote));settings["png_decode_budget_bytes"]=encodedLimit+1;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));assert(configure());settings["png_decode_budget_bytes"]=encodedLimit;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));files.statOps=files.readOps=0;assert(!configure()&&!strcmp(uiError,"large_png_requires_streamable_asset")&&!files.statOps&&!files.readOps);assert(!files.write("/cached.png",0,data.data(),data.size(),true,wrote));settings["png_decode_budget_bytes"]=previousPng;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));assert(configure());
  // A two-entry budget evicts the least recently used source, not a warm one.
  assert(!files.write("/second.png",0,data.data(),data.size(),true,wrote));assert(!files.write("/third.png",0,data.data(),data.size(),true,wrote));clearUiImageMetadata();assert(configure());auto entryBytes=uiImageMetadataBytes;configureUiImageCache(uint32_t(entryBytes*2));node["imageProps"]["src"]="/storage/ram/second.png";assert(configure());node["imageProps"]["src"]="/storage/ram/cached.png";assert(configure());node["imageProps"]["src"]="/storage/ram/third.png";assert(configure());node["imageProps"]["src"]="/storage/ram/cached.png";files.statOps=files.readOps=0;assert(configure()&&!files.statOps&&!files.readOps);node["imageProps"]["src"]="/storage/ram/second.png";assert(configure()&&files.statOps&&files.readOps);configureUiImageCache(65536);node["imageProps"]["src"]="/storage/ram/cached.png";assert(configure());
  // A changing frame on one provider keeps an unrelated provider's decoded
  // PNG resident. Each source still passes its own current lease/epoch checks.
  {std::vector<uint8_t> dynamic(12+16*16*2);dynamic[0]=0x19;dynamic[1]=0x12;dynamic[4]=dynamic[6]=16;dynamic[8]=32;
   const char*mixed=R"({"version":"0.1.1","assets":[{"type":"viewScreen","style":{"padding":0},"children":[{"type":"image","imageProps":{"src":"/storage/ram/dynamic.bin"}},{"type":"image","placement":{"x":32,"y":0,"width":64,"height":64},"imageProps":{"src":"/replacement-images/cached.png","fit":"contain"}}]}]})";assert(!files.write("/mixed-ui.json",0,reinterpret_cast<const uint8_t*>(mixed),strlen(mixed),true,wrote));
   UiSink mixedSink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};auto frame=[&](uint8_t value){dynamic[12]=value;assert(!files.write("/dynamic.bin",0,dynamic.data(),dynamic.size(),true,wrote));assert(prepareUi("/storage/ram/mixed-ui.json"));assert(renderUiRegion({0,0,boardpack::DISPLAY_WIDTH-1,63},mixedSink,true));};frame(1);replacementImages.readOps=0;frame(2);std::cerr<<"Cross-provider static PNG rereads="<<replacementImages.readOps<<"\n";assert(replacementImages.readOps==0);assert(!files.remove("/dynamic.bin")&&!files.remove("/mixed-ui.json"));
  }
  // Source modification is checked even when the previous positive entry fits.
  assert(!files.write("/cached.png",0,(const uint8_t*)"invalid",7,true,wrote));assert(!prepareUi("/storage/ram/cached-ui.json"));settings["image_cache_bytes"]=0;assert(!policies.apply(policy.as<JsonObjectConst>(),result.to<JsonObject>()));assert(prepareUi("/system/ui/home.json"));
  assert(!files.remove("/second.png")&&!files.remove("/third.png"));assert(!files.resize("/cached.png",7));
 }
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
 core.add({"/sys/ui","font_check,read,app_load,open,update,refresh,back,close,font_register,font_unregister,fonts", "",uiHandler,uiServiceDescription});
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
 // Focus/navigation discovery uses the same completed geometry and pagination.
 a.clear();a["view_id"]=hitView;a["limit"]=1;assert(!uiHandler("elements",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["elements"].size()==1&&r["more"]==true&&r["elements"][0]["id"]=="canvas"&&r["rotation"]==90);a["cursor"]=r["cursor"];assert(!uiHandler("elements",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["elements"][0]["id"]=="launch"&&r["elements"][0]["x"]==10&&r["elements"][0]["height"]==80);a["cursor"]=r["cursor"];assert(!uiHandler("elements",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["elements"][0]["id"]=="caption"&&!r["more"].as<bool>());a["limit"]=9;assert(!strcmp(uiHandler("elements",a.as<JsonObjectConst>(),r.to<JsonObject>()),"ui_elements_page"));
 a.clear();a["view_id"]=hitView;assert(!uiHandler("close",a.as<JsonObjectConst>(),r.to<JsonObject>()));resetUiRenderer();assert(core.resources().promised()==0);
 // A complete 8 MiB font remains a filesystem resource. Previously absent
 // Chinese characters and non-preset sizes use its outlines, not compiled text.
 a.clear();assert(!uiHandler("capabilities",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["external_fonts"]=="truetype-file,nmf1-indexed-1bpp"&&r["scalable_font_sizes"]["minimum"]==8&&r["scalable_font_sizes"]["maximum"]==96);
 vfs.add(completeFonts);const char*fullFontUi=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"label","style":{"font":"/fonts/complete.ttf","fontSize":19},"labelProps":{"text":"实验触摸录音 阈值蝴蝶龘"}},{"type":"label","style":{"font":"/fonts/complete.ttf","fontSize":23},"labelProps":{"text":"实验触摸录音 阈值蝴蝶龘"}},{"type":"label","style":{"font":"/fonts/complete.ttf","fontSize":29},"labelProps":{"text":"实验触摸录音 阈值蝴蝶龘"}}]}]})";
 assert(!nm::localTextSupported("阈值蝴蝶龘",24));assert(!files.write("/complete-font-ui.json",0,reinterpret_cast<const uint8_t*>(fullFontUi),strlen(fullFontUi),true,wrote));
 assert(prepareUi("/storage/ram/complete-font-ui.json"));for(int x=0;x<480;x+=16)assert(renderUiBand(x,stripe));std::cerr<<"Full font: missing="<<uiMissingGlyphs<<" largest="<<completeFonts.largestRead<<" read="<<completeFonts.readBytes<<" live="<<uiMemoryUsed()<<"\n";assert(uiMissingGlyphs==0&&completeFonts.largestRead<=4096&&completeFonts.readBytes<1048576);assert(!vfs.canMutate("/fonts/complete.ttf"));
 // A page without a font override follows the system font even after restoring
 // an unrelated saved preference. No application writes global configuration.
 assert(policies.defaults(R"({"system":{"/sys/ui":{"font_file":"/fonts/complete.ttf"}}})"));
 assert(policies.restore(R"({"system":{"/sys/ui":{"font_cache_glyphs":64}}})"));
 const char*inheritedFontUi=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"container","style":{"fontSize":19},"children":[{"type":"label","labelProps":{"text":"阈值蝴蝶龘"}}]}]}]})";
 assert(!files.write("/inherited-font-ui.json",0,reinterpret_cast<const uint8_t*>(inheritedFontUi),strlen(inheritedFontUi),true,wrote));
 assert(prepareUi("/storage/ram/inherited-font-ui.json")&&renderUiBand(0,stripe)&&uiMissingGlyphs==0);
 // Font defaults belong to a presentation transaction. Validation can prepare
 // explicitly styled external fonts while a new system default is pending;
 // validation and rejected candidates preserve the current tree's snapshot.
 {auto tree=uiRoot;auto before=uiFontPolicy;
  assert(!strcmp(before.file,"/fonts/complete.ttf")&&before.cacheGlyphs==64);
  assert(policies.defaults(R"({"system":{"/sys/ui":{"font_file":"/fonts/missing.ttf","font_face_cache":3}}})"));
  assert(prepareUi("/storage/ram/complete-font-ui.json",UiView{},true));
  assert(uiRoot==tree&&!strcmp(uiFontPolicy.file,before.file)&&uiFontPolicy.cacheFaces==before.cacheFaces);
  assert(!prepareUi("/storage/ram/inherited-font-ui.json"));
  assert(uiRoot==tree&&!strcmp(uiFontPolicy.file,before.file)&&uiFontPolicy.cacheGlyphs==64);
  assert(renderUiBand(0,stripe));
  // An ordinary app can explicitly select the compiled family even when the
  // configured external default is unavailable. It has no special privileges.
  const char*builtinUi=R"({"version":"0.1.1","assets":[{"type":"viewScreen","children":[{"type":"label","style":{"font":"builtin","fontSize":16},"labelProps":{"text":"WishOS Home"}}]}]})";
  JsonDocument builtinDoc;assert(!deserializeJson(builtinDoc,builtinUi));assert(prepareUiDocument("/builtin.json",builtinDoc.as<JsonObjectConst>(),UiView{},false));assert(renderUiBand(0,stripe)&&uiMissingGlyphs==0);
  a.clear();assert(!uiHandler("fonts",a.as<JsonObjectConst>(),r.to<JsonObject>()));unsigned builtinCount=0;bool systemListed=false;for(auto f:r["fonts"].as<JsonArrayConst>()){if(f["id"]=="builtin")++builtinCount;if(f["id"]=="system")systemListed=true;}assert(builtinCount==sizeof fontSizes&&systemListed);
  assert(policies.defaults(R"({"system":{"/sys/ui":{"font_file":"/fonts/complete.ttf","font_face_cache":3}}})"));
  assert(policies.restore(R"({"system":{"/sys/ui":{"font_cache_glyphs":17}}})"));
  assert(prepareUi("/storage/ram/inherited-font-ui.json")&&renderUiBand(0,stripe));
  assert(!strcmp(uiFontPolicy.file,"/fonts/complete.ttf")&&uiFontPolicy.cacheGlyphs==17&&uiFontPolicy.cacheFaces==3);
  bool matched=false;for(auto family=uiVectorFamilies;family;family=family->next)for(auto face=family->faces;face;face=face->next)if(uiUsesFont(uiRoot,face->font)){assert(face->cacheGlyphs==17);matched=true;}assert(matched);
 }
 assert(policies.defaults("{}"));assert(policies.restore("{}"));
 JsonDocument fontPolicy;fontPolicy["changes"][0]["resource"]="/sys/ui";fontPolicy["changes"][0]["settings"]["font_file"]="/fonts/complete.ttf";assert(!policies.apply(fontPolicy.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/system/ui/home.json"));assert(renderUiBand(0,stripe)&&uiMissingGlyphs==0);
 a.clear();a["id"]="whole-family";a["file"]="/fonts/complete.ttf";assert(!uiHandler("font_register",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(!strcmp(uiHandler("font_unregister",a.as<JsonObjectConst>(),r.to<JsonObject>()),"font_in_use"));
 {JsonDocument bench;bench["version"]="0.1.1";auto screen=bench["assets"].to<JsonArray>().add<JsonObject>();screen["type"]="viewScreen";auto label=screen["children"].to<JsonArray>().add<JsonObject>();label["type"]="label";label["style"]["fontSize"]=16;std::string text;for(char c=32;c<127;++c)text+=c;label["labelProps"]["text"]=text;std::string encoded;serializeJson(bench,encoded);assert(!files.write("/cache-benchmark.json",0,(const uint8_t*)encoded.data(),encoded.size(),true,wrote));
  auto redraw=[&](unsigned cache){fontPolicy["changes"][0]["settings"]["font_cache_glyphs"]=cache;assert(!policies.apply(fontPolicy.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/storage/ram/cache-benchmark.json"));UiSink sink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};assert(renderUiRegion({0,0,479,719},sink));auto before=completeFonts.readBytes;assert(renderUiRegion({0,0,479,719},sink));return completeFonts.readBytes-before;};
  auto small=redraw(32),large=redraw(128);std::cerr<<"Font warm-render source reads: cache32="<<small<<" cache128="<<large<<"\n";assert(small>large&&uiMissingGlyphs==0);fontPolicy["changes"][0]["settings"]["font_cache_glyphs"]=32;
 }
 fontPolicy["changes"][0]["settings"]["font_file"]="";assert(!policies.apply(fontPolicy.as<JsonObjectConst>(),r.to<JsonObject>()));assert(prepareUi("/system/ui/home.json"));assert(!uiHandler("font_unregister",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(vfs.canMutate("/fonts/complete.ttf"));
 fontFamilyScenarios();
 completeFonts.failOnce=true;auto kept=uiRoot;assert(!prepareUi("/storage/ram/complete-font-ui.json"));assert(uiRoot==kept&&!strcmp(uiError,"font_fixture_io")&&vfs.canMutate("/fonts/complete.ttf"));
 size_t fontBoundaries=0;for(size_t pass=0;pass==0||pass<=fontBoundaries;++pass){uiFailAt=SIZE_MAX;assert(prepareUi("/system/ui/home.json"));kept=uiRoot;uiAllocCalls=0;uiFailAt=pass?pass-1:SIZE_MAX;bool accepted=prepareUi("/storage/ram/complete-font-ui.json");if(!pass)fontBoundaries=uiAllocCalls;uiFailAt=SIZE_MAX;if(!accepted)assert(uiRoot==kept);assert(prepareUi("/system/ui/home.json"));assert(vfs.canMutate("/fonts/complete.ttf"));}
 assert(prepareUi("/storage/ram/complete-font-ui.json"));JsonDocument fontMemory;fontMemory["memory_limit_bytes"]=uint32_t(core.resources().promised()+1);assert(!core.resources().configure(fontMemory.as<JsonObjectConst>()));assert(!renderUiBand(32,stripe));assert(!strcmp(uiError,"ui_memory_budget"));fontMemory["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(fontMemory.as<JsonObjectConst>()));assert(renderUiBand(32,stripe));
 resetUiRenderer();assert(core.resources().promised()==0);std::cout<<"Complete font streaming, arbitrary sizes, default selection, I/O rejection and font allocation rollback passed\n";
 std::cout<<"Full-screen image bytes="<<bin.size()<<", largest provider read="<<files.largestRead<<", image reads="<<imageReadBytes<<", image read calls="<<imageReadOps<<"\n";
 // An opaque sibling overlay blocks controls behind it. Hit IDs bubble
 // through the selected branch only, while uncovered controls remain usable.
 {const char*overlay=R"({"version":"0.1.1","assets":[{"type":"viewScreen","id":"root","style":{"padding":0},"children":[{"type":"button","id":"under","placement":{"x":10,"y":10,"width":100,"height":80},"style":{"padding":0}},{"type":"container","placement":{"x":10,"y":10,"width":50,"height":80},"style":{"bgColor":"#ffffff","padding":0}}]}]})";assert(!files.write("/overlay.json",0,(const uint8_t*)overlay,strlen(overlay),true,wrote));a.clear();a["path"]="/storage/ram/overlay.json";a["history"]="replace";assert(!uiHandler("open",a.as<JsonObjectConst>(),r.to<JsonObject>()));auto view=r["view_id"].as<uint32_t>();assert(prepareUi("/storage/ram/overlay.json",uiDocument(view)->view));presentationState.completed(presentationState.stamp(view));commitUiGeometry(presentationState.stamp(view));
  a.clear();a["x"]=20;a["y"]=20;assert(!uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["ids"].size()==1&&r["ids"][0]=="root");a["x"]=90;assert(!uiHandler("hit_test",a.as<JsonObjectConst>(),r.to<JsonObject>()));assert(r["ids"][0]=="under"&&r["ids"][1]=="root");}
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
