#include "UiMemory.h"
#include "PackedFont.h"
#include "Core.h"
#include "Policy.h"
#include "PresentationState.h"
#include "DisplayScheduler.h"
#include "ScriptHost.h"
#include "display/NativeText.h"
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
#define STB_TRUETYPE_IMPLEMENTATION
#include <src/libs/tiny_ttf/stb_truetype_htcw.h>
uint32_t millis(){return 1;}
namespace notesos {
namespace boardpack {constexpr unsigned DISPLAY_WIDTH=320,DISPLAY_HEIGHT=240;}
static Core core;static Vfs vfs;static char appScope[48]="system";
static const Setting settings[]={{"/sys/ui","font_file","string","\"\"","",0,255},{"/sys/ui","png_decode_budget_bytes","integer","16384","",1,1048576}};
static Policies policies(settings,2);
class Fonts:public FileProvider {public:bool fail=false;size_t maxRead=0;std::string alias;std::vector<uint8_t>unsupported;
 Fonts():FileProvider("fonts","/fonts","fixture",true,false,false){}
 bool available()const override{return true;}void describe(JsonObject)const override{}
 const char*stat(const char*path,FileInfo&i)override{if(!strcmp(path,"/unsupported.ttf")){i={unsupported.size(),false};return nullptr;}if(strcmp(path,"/complete.ttf")&&path!=alias)return "not_found";std::ifstream f("hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf",std::ios::binary|std::ios::ate);assert(f);i={uint64_t(f.tellg()),false};return nullptr;}
 const char*read(const char*p,uint64_t at,uint8_t*out,size_t n,size_t&got)override{got=0;if(fail)return "font_io";if(!strcmp(p,"/unsupported.ttf")){got=at>=unsupported.size()?0:std::min<uint64_t>(n,unsupported.size()-at);memcpy(out,unsupported.data()+std::min<uint64_t>(at,unsupported.size()),got);return nullptr;}FileInfo i;if(auto e=stat(p,i))return e;maxRead=std::max(maxRead,n);std::ifstream f("hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf",std::ios::binary);f.seekg(at);f.read(reinterpret_cast<char*>(out),n);got=f.gcount();return nullptr;}
 const char*write(const char*,uint64_t,const uint8_t*,size_t,bool,size_t&n)override{n=0;return "read_only";}
 const char*list(const char*,uint32_t,uint32_t,JsonObject)override{return "unsupported_operation";}const char*remove(const char*)override{return "read_only";}const char*mkdir(const char*)override{return "read_only";}const char*rename(const char*,const char*)override{return "read_only";}
};static Fonts fonts;static MemoryFiles files("ram","/ram","ram");
static bool resolveFile(const char*in,char*full,const char*&rel,FileProvider*&p){if(!normalizePath("/",in,full,256))return false;p=vfs.resolve(full,rel);return p;}
#include "UiService.h"
static const char*defaultUiDriver(){return "fixture";}static const char*validateUiDriver(const char*){return nullptr;}
static DisplayAdmission queueUi(const char*,const char*,bool,JsonObject,const char*,const UiView*,PresentationState::Stamp){return {};}
}
static void put(std::vector<uint8_t>&b,size_t at,uint32_t n,unsigned bytes){for(unsigned i=0;i<bytes;++i)b[at+i]=uint8_t(n>>(8*(bytes-i-1)));}
static std::vector<uint8_t> synthetic(unsigned format){
 std::vector<uint8_t>b(640);put(b,0,0x10000,4);put(b,4,7,2);const char*tags[]={"head","hhea","hmtx","maxp","cmap","loca","glyf"};for(unsigned i=0;i<7;++i){memcpy(b.data()+12+i*16,tags[i],4);put(b,20+i*16,i==4?128:620,4);put(b,24+i*16,i==4?384:0,4);}
 put(b,130,1,2);put(b,132,3,2);put(b,134,10,2);put(b,136,12,4);size_t m=140;put(b,m,format,2);
 if(format==0){put(b,m+2,256,2);b[m+6+'A']=9;}
 else if(format==6){put(b,m+2,14,2);put(b,m+6,'A',2);put(b,m+8,2,2);put(b,m+10,9,2);}
 else if(format==4){put(b,m+2,36,2);put(b,m+6,4,2);put(b,m+8,4,2);put(b,m+10,1,2);put(b,m+14,'A',2);put(b,m+16,0xffff,2);put(b,m+20,'A',2);put(b,m+22,0xffff,2);put(b,m+24,17,2);put(b,m+26,1,2);put(b,m+28,4,2);put(b,m+32,9,2);}
 else if(format==12||format==13){put(b,m+4,28,4);put(b,m+12,1,4);put(b,m+16,'A',4);put(b,m+20,'B',4);put(b,m+24,9,4);}
 else put(b,m+2,6,2);return b;
}
static void unsupportedFont(notesos::Fonts&fonts){std::ifstream file("hardware/agent-os/runtime/fonts/NotesSansSC-Regular.ttf",std::ios::binary);fonts.unsupported.assign(std::istreambuf_iterator<char>(file),{});auto&b=fonts.unsupported;auto number=[&](size_t at,unsigned n){uint32_t v=0;while(n--)v=v*256+b.at(at++);return v;};size_t cmap=0;for(unsigned i=0;i<number(4,2);++i)if(!memcmp(b.data()+12+i*16,"cmap",4)){cmap=number(20+i*16,4);break;}assert(cmap);size_t selected=0;for(unsigned i=0;i<number(cmap+2,2);++i){size_t at=cmap+4+i*8;auto platform=number(at,2),encoding=number(at+2,2);if(platform==0||(platform==3&&(encoding==1||encoding==10)))selected=cmap+number(at+4,4);}assert(selected&&number(selected,2)==12);put(b,selected,2,2);}
int main(int argc,char**argv){using namespace notesos;assert(vfs.add(files)&&vfs.add(fonts));core.add({"/sys/ui","font_check,read,fonts,font_register,font_unregister","",uiHandler,uiServiceDescription,"font_check,read,fonts"});
 if(argc>1&&!strcmp(argv[1],"--unsupported-cmap-probe")){unsupportedFont(fonts);if(auto error=validateVectorFont("/fonts/unsupported.ttf")){std::cerr<<"Native validator rejected unsupported cmap: "<<error<<"\n";return 0;}std::cerr<<"Native validator accepted unsupported cmap; invoking actual tinyTTF glyph\n";assert(initUi());auto font=vectorFont("/fonts/unsupported.ttf",24);assert(font);lv_font_glyph_dsc_t glyph{};bool found=font->get_glyph_dsc(font,&glyph,65,0);std::cerr<<"Unexpected glyph return "<<found<<"\n";return 2;}
 JsonDocument args,result;auto call=[&](){result.clear();return uiHandler("font_check",args.as<JsonObjectConst>(),result.to<JsonObject>());};
 // Discovery works before LVGL exists, reads bounded font windows and releases leases.
 args["font"]="/fonts/complete.ttf";
 auto points=args["codepoints"].to<JsonArray>();for(uint32_t cp:{45,0x2212,0x9f98,0,1,10,13,31,0x200c,0xf8ff})points.add(cp);
 auto before=core.resources().promised();assert(!call());assert(!uiDisplay&&!uiRoot&&!uiVectorFamilies&&core.resources().promised()==before);assert(result["missing_codepoints"].size()==1&&result["missing_codepoints"][0]==0x2212);assert(result["resolved"]["provider"]=="fonts"&&fonts.maxRead<=256&&vfs.canMutate("/fonts/complete.ttf"));
 assert(policies.defaults(R"({"system":{"/sys/ui":{"font_file":"/fonts/complete.ttf"}}})"));args.remove("font");assert(!call()&&result["resolved"]["font"]=="system");
 JsonDocument description;uiServiceDescription(description.to<JsonObject>());assert(description["font_check"]["method"]=="font_check"&&description["font_check"]["max_codepoints"]==uiFontCheckLimit);
  {Vfs::Access write(vfs,true);assert(!write.add("/fonts/complete.ttf"));assert(!strcmp(call(),"file_in_use"));}assert(!call());
 // Fixed renderer and independent cmap query agree, including tinyTTF zero-width controls.
 assert(initUi());auto face=vectorFont("/fonts/complete.ttf",24);assert(face);for(auto cp:points){lv_font_glyph_dsc_t glyph{};bool actual=face->get_glyph_dsc(face,&glyph,cp,0);bool missing=false;for(auto m:result["missing_codepoints"].as<JsonArrayConst>())if(m==cp)missing=true;assert(actual!=missing);}
 // Native registration and real UI admission share the query's format boundary.
 JsonDocument live;live["version"]="0.1.1";auto label=live["assets"].to<JsonArray>().add<JsonObject>();label["type"]="viewScreen";auto child=label["children"].to<JsonArray>().add<JsonObject>();child["type"]="label";child["style"]["font"]="/fonts/complete.ttf";child["style"]["fontSize"]=24;child["labelProps"]["text"]="WishOS";
 assert(prepareUiDocument("/live-font.json",live.as<JsonObjectConst>(),UiView{},false));auto retainedRoot=uiRoot;auto retainedBytes=core.resources().promised();unsigned registered=0;for(auto&entry:fontFiles)if(entry.font)++registered;
 unsupportedFont(fonts);JsonDocument badRegistration,badResult;badRegistration["id"]="unsupported";badRegistration["file"]="/fonts/unsupported.ttf";assert(!strcmp(uiHandler("font_register",badRegistration.as<JsonObjectConst>(),badResult.to<JsonObject>()),"font_cmap_format_not_supported"));unsigned afterRegistered=0;for(auto&entry:fontFiles)if(entry.font)++afterRegistered;assert(registered==afterRegistered&&vfs.canMutate("/fonts/unsupported.ttf"));
 JsonDocument badUi;badUi.set(live);badUi["assets"][0]["children"][0]["style"]["font"]="/fonts/unsupported.ttf";assert(!prepareUiDocument("/bad-font.json",badUi.as<JsonObjectConst>(),UiView{},false)&&!strcmp(uiError,"font_cmap_format_not_supported")&&uiRoot==retainedRoot&&vfs.canMutate("/fonts/unsupported.ttf"));JsonDocument badQuery;badQuery["font"]="/fonts/unsupported.ttf";badQuery["codepoints"].to<JsonArray>().add(65);assert(!strcmp(uiHandler("font_check",badQuery.as<JsonObjectConst>(),badResult.to<JsonObject>()),"font_cmap_format_not_supported")&&uiRoot==retainedRoot);assert(core.resources().promised()==retainedBytes);UiSink sink{nullptr,[](void*,const lv_area_t&,const uint16_t*){}};assert(renderUiRegion({0,0,319,63},sink));
 auto root=uiRoot;auto family=uiVectorFamilies;auto policy=uiFontPolicy;const char*error=uiError;
 args["font"]="/fonts/complete.ttf";fonts.fail=true;assert(!strcmp(call(),"font_io"));fonts.fail=false;assert(uiRoot==root&&uiVectorFamilies==family&&uiError==error&&!memcmp(&policy,&uiFontPolicy,sizeof policy));
 JsonDocument budget;budget["memory_limit_bytes"]=uint32_t(core.resources().promised()+1);assert(!core.resources().configure(budget.as<JsonObjectConst>()));assert(!strcmp(call(),"ui_memory_budget"));budget["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(budget.as<JsonObjectConst>()));assert(!call());
 args["codepoints"][0]=0xd800;assert(!strcmp(call(),"font_check_codepoint"));args["codepoints"][0]=-1;assert(!strcmp(call(),"font_check_codepoint"));args["codepoints"][0]=0x110000;assert(!strcmp(call(),"font_check_codepoint"));args["codepoints"][0]=45;args["font"]=std::string("builtin\0tail",12);assert(!strcmp(call(),"ui_font_reference"));args["font"]="builtin";args["size"]=19;assert(!strcmp(call(),"font_or_size_not_registered"));args["size"]=24;assert(!call());
 points=args["codepoints"].to<JsonArray>();for(size_t i=0;i<=uiFontCheckLimit;++i)points.add(0x10ffff);assert(!strcmp(call(),"font_check_codepoints_limit"));points.remove(points.size()-1);assert(!call()&&measureJson(result)<MAX_WIRE-256);
 // Actual pinned stb is the oracle for every supported cmap format. In format4
 // its nonzero range-offset branch returns the stored glyph without idDelta.
 size_t wrote=0;for(unsigned format:{0,4,6,12,13}){auto bytes=synthetic(format);assert(!files.write("/cmap.ttf",0,bytes.data(),bytes.size(),true,wrote));TrueTypeCoverage reader(files,"/cmap.ttf");assert(!reader.open());stbtt_fontinfo fixed{};fixed.data=bytes.data();fixed.index_map=140;for(uint32_t cp:{0,64,65,66,67,0xffff,0x10000}){uint32_t glyph=0;assert(!reader.glyph(cp,glyph));assert(glyph==unsigned(stbtt_FindGlyphIndex(&fixed,cp)));}}
 // Encoding-record order is the pinned renderer's selection rule.
 auto ordered=synthetic(6);put(ordered,130,2,2);put(ordered,136,20,4);put(ordered,140,0,2);put(ordered,142,0,2);put(ordered,144,80,4);put(ordered,148,6,2);put(ordered,150,12,2);put(ordered,154,65,2);put(ordered,156,1,2);put(ordered,158,9,2);put(ordered,208,6,2);put(ordered,210,12,2);put(ordered,214,65,2);put(ordered,216,1,2);put(ordered,218,23,2);
 assert(!files.write("/ordered.ttf",0,ordered.data(),ordered.size(),true,wrote));{TrueTypeCoverage reader(files,"/ordered.ttf");assert(!reader.open());uint32_t glyph=0;assert(!reader.glyph(65,glyph)&&glyph==23);stbtt_fontinfo fixed{};fixed.data=ordered.data();fixed.index_map=208;assert(glyph==unsigned(stbtt_FindGlyphIndex(&fixed,65)));}
 // NMF uses the same indexed lookup, and registered names keep caller ownership.
 std::vector<uint8_t>packed(48);memcpy(packed.data(),"NMF1",4);packed[4]=2;packed[16]='?';packed[20]=24;packed[25]=1;packed[32]='A';packed[36]=24;packed[41]=1;assert(!files.write("/font.nmf",0,packed.data(),packed.size(),true,wrote));
 JsonDocument registration;registration["id"]="packed";registration["file"]="/ram/font.nmf";registration["size"]=24;auto owner=core.acquireOwner();assert(!core.invoke("/sys/ui","font_register",registration.as<JsonObjectConst>(),result.to<JsonObject>(),owner));args.clear();args["font"]="packed";points=args["codepoints"].to<JsonArray>();for(unsigned cp:{65,66,10,13})points.add(cp);result.clear();assert(!core.invoke("/sys/ui","font_check",args.as<JsonObjectConst>(),result.to<JsonObject>(),owner));assert(result["missing_codepoints"].size()==1&&result["missing_codepoints"][0]==66);auto unrelated=core.acquireOwner();assert(!strcmp(core.invoke("/sys/ui","font_check",args.as<JsonObjectConst>(),result.to<JsonObject>(),unrelated),"font_or_size_not_registered"));core.releaseOwner(unrelated);assert(!core.invoke("/sys/ui","font_unregister",registration.as<JsonObjectConst>(),result.to<JsonObject>(),owner));core.releaseOwner(owner);
 // The resource's read-method declaration permits native coverage in preparation.
 ScriptHost runtime(core,vfs);JsonDocument load;load["source"]=R"lua(function on_prepare() local r=notes.call('/sys/ui','font_check',{font='/fonts/complete.ttf',codepoints={45,8722}}); assert(r.ok and r.result.missing_codepoints[1]==8722) end)lua";assert(!runtime.invoke("load",load.as<JsonObjectConst>(),result.to<JsonObject>()));JsonDocument empty;assert(!runtime.invoke("stop",empty.as<JsonObjectConst>(),result.to<JsonObject>()));
 // Long quoted filenames exercise JSON escaping in resolved metadata at the
 // advertised batch maximum, including the outer RPC reply's reserved bytes.
 fonts.alias="/"+std::string(244,'\"')+".ttf";args.clear();args["font"]="/fonts"+fonts.alias;points=args["codepoints"].to<JsonArray>();for(size_t i=0;i<uiFontCheckLimit;++i)points.add(0x10ffff);assert(!call()&&result["missing_codepoints"].size()==uiFontCheckLimit&&measureJson(result)<MAX_WIRE-256);{JsonDocument prior;prior.set(result);auto list=prior["missing_codepoints"].as<JsonArray>();while(list.size()<(MAX_WIRE-1024)/8)list.add(0x10ffff);std::cout<<"Escaped-font batch bytes: admitted="<<measureJson(result)<<", former-bound="<<measureJson(prior)<<"\n";assert(measureJson(prior)>MAX_WIRE);}
 auto invalid=synthetic(2);assert(!files.write("/cmap.ttf",0,invalid.data(),invalid.size(),true,wrote));{TrueTypeCoverage reader(files,"/cmap.ttf");assert(!strcmp(reader.open(),"font_cmap_format_not_supported"));}
 auto valid=synthetic(12);for(size_t n:{size_t(0),size_t(10),size_t(125),size_t(155),size_t(511)}){assert(!files.write("/short.ttf",0,valid.data(),n,true,wrote));TrueTypeCoverage reader(files,"/short.ttf");assert(reader.open());}
 invalid=synthetic(4);put(invalid,168,0xffff,2);assert(!files.write("/bad.ttf",0,invalid.data(),invalid.size(),true,wrote));{TrueTypeCoverage reader(files,"/bad.ttf");assert(!reader.open());uint32_t glyph;assert(!strcmp(reader.glyph(65,glyph),"font_cmap_range"));}
 resetUiRenderer();assert(core.resources().promised()==0);std::cout<<"Read-only font discovery, real glyph agreement, bounded cmap formats and failure isolation passed\n";
}
