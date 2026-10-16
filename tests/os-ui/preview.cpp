// Render ordinary filesystem UI through the exact portable UI service.
#include "UiMemory.h"
#include "PackedFont.h"
#include "Core.h"
#include "Policy.h"
#include "PresentationState.h"
#include "DisplayScheduler.h"
#include "display/NativeText.h"
#include "drivers/esp32-services/PosixFileProvider.h"
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include <fstream>
#include <iostream>
#include <vector>
uint32_t millis(){static uint32_t t=0;return ++t;}
namespace notesos {
namespace boardpack {constexpr unsigned DISPLAY_WIDTH=320,DISPLAY_HEIGHT=240;}
static Core core;static Vfs vfs;static char appScope[48]="system";
static const Setting settings[]={Setting{"/sys/ui","font_file","string","\"/fonts/NotesSansSC-Regular.ttf\"","",0,255}.inSystem(),{"/sys/ui","png_decode_budget_bytes","integer","262144","",1,1048576}};
static Policies policies(settings,2);
static bool resolveFile(const char*in,char*full,const char*&rel,FileProvider*&p){if(!normalizePath("/",in,full,256))return false;p=vfs.resolve(full,rel);return p;}
#include "UiService.h"
static const char*defaultUiDriver(){return "preview.rgb565";}static const char*validateUiDriver(const char*){return nullptr;}
static DisplayAdmission queueUi(const char*,const char*,bool,JsonObject,const char*,const UiView*,PresentationState::Stamp){return {};}
}
int main(int argc,char**argv){using namespace notesos;if(argc<4){std::cerr<<"usage: preview ASSET_DIRECTORY UI_RELATIVE_PATH OUTPUT.ppm [VALUES.json]\n";return 2;}bool ready=true;PosixFileProvider assets("assets","/preview","host",false,ready,argv[1],"","host",1),fonts("fonts","/fonts","host",false,ready,"hardware/agent-os/runtime/fonts","","host",1);assets.writable=fonts.writable=false;vfs.add(assets);vfs.add(fonts);std::string file="/preview/"+std::string(argv[2]);UiView model;if(argc>4){std::ifstream in(argv[4]);std::string values((std::istreambuf_iterator<char>(in)),{});if(values.size()>1024)return 2;copy(model.values,sizeof model.values,values.c_str());}if(!prepareUi(file.c_str(),model)){std::cerr<<uiError<<"\n";return 1;}std::vector<uint16_t>pixels(320*240);UiSink sink{pixels.data(),[](void*p,const lv_area_t&a,const uint16_t*s){auto dst=static_cast<uint16_t*>(p);for(int y=a.y1;y<=a.y2;++y)for(int x=a.x1;x<=a.x2;++x)dst[y*320+x]=s[(y-a.y1)*(a.x2-a.x1+1)+x-a.x1];}};if(!renderUiRegion({0,0,319,239},sink)){std::cerr<<uiError<<"\n";return 1;}std::ofstream out(argv[3],std::ios::binary);out<<"P6\n320 240\n255\n";for(auto value:pixels){char rgb[3]={char(((value>>11)&31)*255/31),char(((value>>5)&63)*255/63),char((value&31)*255/31)};out.write(rgb,3);}std::cout<<"Rendered "<<file<<" missing="<<uiMissingGlyphs<<"\n";resetUiRenderer();return 0;}
