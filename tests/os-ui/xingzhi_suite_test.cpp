// A second S3 topology: single-button square LCD, independent audio, no card,
// touch, camera or IMU assumptions in its ordinary factory applications.
#include "UiMemory.h"
#include "UiSettings.h"
#include "PackedFont.h"
#include "Policy.h"
#include "PresentationState.h"
#include "ButtonGestures.h"
#include "DisplayScheduler.h"
#include "display/NativeText.h"
#include <Arduino.h>
#include <lvgl.h>
#include <lvgl_private.h>
#include "Core.h"
#include "ScriptHost.h"
#include "FileService.h"
#include "drivers/esp32-services/PosixFileProvider.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <iostream>
#include <vector>
#include "suite_layout_checks.h"
uint32_t millis(){static uint32_t now=0;return ++now;}
namespace notesos {
namespace boardpack {constexpr unsigned DISPLAY_WIDTH=240,DISPLAY_HEIGHT=240;}
static Core core;static Vfs vfs;static ScriptHost*runtime;static FileService*files;
static bool contentValid=true,failSplash=false;static JsonDocument shown;static unsigned launches=0,homes=0,micStarts=0,micStops=0,uiUpdates=0;static const char*snapshotName=nullptr;static std::string launched;static bool ap=false,linkOnline=false,wifiOnline=false;static uint32_t aiId=0;static std::string aiOperation;
static bool requestHeld=false;static uint32_t requestSequence=0;static char appScope[48]="system";static const char*timeText="2026-10-17T13:28:00+0800";
static const Setting settings[]={uiDrawBufferSetting(boardpack::DISPLAY_WIDTH,boardpack::DISPLAY_HEIGHT,"40960"),Setting{"/sys/ui","font_file","string","\"/fonts/NotesSansSC-Regular.ttf\"","",0,255},{"/sys/ui","png_decode_budget_bytes","integer","131072","",1,1048576},{"/sys/ui","image_cache_bytes","integer","65536","",0,1048576}};
static Policies policies(settings,sizeof settings/sizeof *settings);
static bool resolveFile(const char*in,char*full,const char*&relative,FileProvider*&provider){if(!normalizePath("/",in,full,256))return false;provider=vfs.resolve(full,relative);return provider;}
#include "UiService.h"
static const char*defaultUiDriver(){return "fixture.rgb565";}
static const char*validateUiDriver(const char*d){return strcmp(d,defaultUiDriver())?"unknown_driver":nullptr;}
static DisplayAdmission queueUi(const char*path,const char*,bool,JsonObject out,const char*,const UiView*view,PresentationState::Stamp stamp){
 if(failSplash&&strstr(path,"splash.json")){JsonDocument e;e["view_id"]=stamp.view;e["view_revision"]=stamp.revision;core.emit("/dev/display","failed",e.as<JsonObjectConst>(),view->owner);out["job_id"]=++uiUpdates;return {};}
 if(!prepareUi(path,*view)){std::cerr<<"render: "<<uiError<<"\n";return DisplayAdmission::rejected(uiError);}
 std::vector<uint16_t>pixels;if(getenv("WISHOS_CUBE_SNAPSHOTS")&&snapshotName)pixels.resize(240*240);
 UiSink sink{pixels.empty()?nullptr:pixels.data(),[](void*p,const lv_area_t&a,const uint16_t*source){if(!p)return;auto out=static_cast<uint16_t*>(p);for(int y=a.y1;y<=a.y2;++y)for(int x=a.x1;x<=a.x2;++x){assert(x>=0&&x<240&&y>=0&&y<240);out[y*240+x]=source[(y-a.y1)*(a.x2-a.x1+1)+x-a.x1];}}};if(!renderUiRegion({0,0,239,239},sink))return DisplayAdmission::rejected(uiError);contentValid=true;assert(!uiMissingGlyphs);checkSuiteLayout(uiRoot,path);if(strstr(path,"help")){for(auto n=uiNodeRefs;n;n=n->next)if(!strcmp(n->id,"message")){lv_point_t textSize;lv_text_get_size(&textSize,lv_label_get_text(n->node),lv_obj_get_style_text_font(n->node,0),0,lv_obj_get_style_text_line_space(n->node,0),lv_obj_get_content_width(n->node),LV_TEXT_FLAG_NONE);assert(textSize.y<=lv_obj_get_content_height(n->node));}}if(!pixels.empty()){std::filesystem::create_directories(getenv("WISHOS_CUBE_SNAPSHOTS"));std::ofstream image(std::string(getenv("WISHOS_CUBE_SNAPSHOTS"))+"/"+snapshotName+".ppm",std::ios::binary);image<<"P6\n240 240\n255\n";for(auto value:pixels){char rgb[3]={char(((value>>11)&31)*255/31),char(((value>>5)&63)*255/63),char((value&31)*255/31)};image.write(rgb,3);}snapshotName=nullptr;}assert(!deserializeJson(shown,view->values));presentationState.completed(stamp);commitUiGeometry(stamp);out["job_id"]=++uiUpdates;JsonDocument e;e["view_id"]=stamp.view;e["view_revision"]=stamp.revision;core.emit("/dev/display","completed",e.as<JsonObjectConst>(),view->owner);return {};
}
}
int main(){using namespace notesos;

 char temporary[]="/tmp/wishos-cube-suite-XXXXXX";assert(mkdtemp(temporary));
 std::filesystem::copy("hardware/agent-os/runtime/xingzhi-cube-suite/assets",temporary,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
 std::filesystem::create_directory(std::string(temporary)+"/brand");std::filesystem::copy_file("hardware/agent-os/runtime/lckfb-suite/assets/brand/lockup-compact.png",std::string(temporary)+"/brand/lockup-compact.png");
 for(auto name:{"voice.lua","sessions.lua","activity.lua","reply.lua","help-content.lua"})std::filesystem::copy_file(std::string("hardware/agent-os/runtime/lckfb-suite/assets/suite/")+name,std::string(temporary)+"/suite/"+name);
 for(auto name:{"boot-logo.png","logo.png","microphone.png","busy-00.png","busy-01.png","busy-02.png","busy-03.png","busy-04.png","busy-05.png","busy-06.png","busy-07.png","busy-08.png","busy-09.png","busy-10.png","busy-11.png"})std::filesystem::copy_file(std::string("hardware/agent-os/runtime/lckfb-suite/assets/brand/")+name,std::string(temporary)+"/brand/"+name);
 std::filesystem::create_directory(std::string(temporary)+"/icons");for(auto name:{"voice","apps","settings","device"})std::filesystem::copy_file(std::string("hardware/agent-os/runtime/lckfb-suite/assets/icons/")+name+".png",std::string(temporary)+"/icons/"+name+".png");
 bool ready=true;PosixFileProvider factory("factory","/factory","host",false,ready,temporary,"","host",1),system("system","/system","host",false,ready,"firmware/WishOS","","host",1);system.writable=false;PosixFileProvider fonts("fonts","/fonts","host",false,ready,"hardware/agent-os/runtime/fonts","","host",1);fonts.writable=false;vfs.add(factory);vfs.add(system);vfs.add(fonts);
 FileService fileApi(vfs,&core);files=&fileApi;ScriptHost host(core,vfs);runtime=&host;
 core.add(Device::bind("/fs",FileService::Methods,"",fileApi,"read,stat,list"));
 core.add({"/sys/ui","font_check,open,update","",uiHandler,uiServiceDescription,"font_check"});
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return core.describe(a["path"]|"",o);},nullptr,"describe"});
 core.add({"/sys/operations","request,read,release","",[](const char*m,JsonObjectConst a,JsonObject o){return core.operation(m,a,o);}});
 core.add({"/sys/apps","read,list,launch,home,voice_input","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"read")){o["voice_input_held"]=requestHeld;o["voice_request_id"]=requestSequence;if(requestSequence){o["arguments"]["record"]=true;o["arguments"]["voice_request_id"]=requestSequence;}}else if(!strcmp(m,"home")){++homes;}else if(!strcmp(m,"launch")){++launches;launched=a["id"]|"";}else if(!strcmp(m,"list")){unsigned start=a["cursor"]|0u,limit=a["limit"]|4u;if(!limit||limit>4)return "application_page_limit";const char*names[]={"launcher","voice","timer","book","clock","notes"};auto all=o["applications"].to<JsonArray>();for(unsigned i=start;i<6&&i<start+limit;++i){auto item=all.add<JsonObject>();item["id"]=names[i];item["title"]=names[i];}o["more"]=start+all.size()<6;o["next_cursor"]=start+all.size();}return nullptr;},nullptr,"read,list"});
 core.add({"/sys/timers","write,remove","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"remove")){core.removeTimer(a["id"]);return nullptr;}return core.timer(a,o);}});
 core.add({"/services/time","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["wall_clock_valid"]=true;o["local_time"]=timeText;return nullptr;},nullptr,"read"});
 core.add({"/dev/battery","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["valid"]=true;o["battery_percent"]=75;return nullptr;},nullptr,"read"});
 core.add({"/dev/display","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["content_valid"]=contentValid;return nullptr;},nullptr,"read"});
 core.add({"/dev/wifi","read,configure","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure"))ap=a["settings"]["ap_enabled"]|false;o["connected"]=wifiOnline;o["ssid"]="Home WiFi";o["ip"]="192.0.2.20";o["mac"]="02:00:00:00:00:02";o["ap_active"]=ap;o["ap_ip"]="192.168.4.1";o["ap_ssid"]="WishOS-D6CA80";return nullptr;},nullptr,"read"});
 core.add({"/dev/mic","read,start,stop","",[](const char*m,JsonObjectConst,JsonObject o)->const char*{if(!strcmp(m,"start")){++micStarts;o["recording_id"]="fixture-recording";}if(!strcmp(m,"stop"))++micStops;return nullptr;},nullptr,"read"});
 core.add({"/dev/speaker","read,start,write,finish,stop","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["playback_id"]=1;return nullptr;},[](JsonObject){},"read"});
 core.add({"/services/link","read","",[](const char*,JsonObjectConst,JsonObject o)->const char*{o["connected"]=linkOnline;return nullptr;},nullptr,"read"});
 core.add({"/services/ai","read,submit,release,cancel","",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"submit")){o["id"]=++aiId;o["request_id"]="fixture:"+std::to_string(aiId);aiOperation=a["input"]["operation"]|"";}else if(!strcmp(m,"read")){o["state"]="completed";o["tasks"].to<JsonArray>();}return nullptr;},[](JsonObject o){asyncContract(o,"submit","/services/ai")["id"]="id";},"read"});
 auto check=[&](){JsonDocument a,o;assert(!host.invoke("read",a.to<JsonObject>(),o.to<JsonObject>()));if(o["fault"]==true){serializeJson(o,std::cerr);abort();}};
 auto load=[&](const char*name){JsonDocument a,o;a["file"]=std::string("/factory/apps/")+name+".lua";JsonDocument descriptor;{std::ifstream file(std::string(temporary)+"/apps/"+name+".json");assert(file);assert(!deserializeJson(descriptor,file));}a["memory_bytes"]=descriptor["memory_bytes"]|131072u;a["instruction_budget"]=descriptor["instruction_budget"]|200000u;a["system_call_budget"]=descriptor["system_call_budget"]|64u;if(auto e=host.invoke("load",a.as<JsonObjectConst>(),o.to<JsonObject>())){std::cerr<<e<<" ";serializeJson(o,std::cerr);abort();}check();};
 // Feed real native gesture facts into the actual restricted Lua runtime.
 ButtonGestures input({"USER"},&host,[](void*p,const char*name,JsonObjectConst data){Event e;strcpy(e.path,"/dev/buttons");copy(e.name,sizeof e.name,name);serializeJson(data,e.data,sizeof e.data);assert(core.emit(e.path,e.name,data));e.sequence=core.sequence();static_cast<ScriptHost*>(p)->event(e);});
 JsonDocument timing,configured;timing["defaults"]["click_window_ms"]=240;assert(!input.configure(timing.as<JsonObjectConst>(),configured.to<JsonObject>()));uint32_t at=1000;
 auto tap=[&](){auto s=presentationState.presented();InputPresentation stamp{s.view,s.revision,s.activation};input.sample(0,true,at,stamp);input.sample(0,true,at+26,stamp);input.sample(0,false,at+80,stamp);input.sample(0,false,at+106,stamp);at+=160;check();};
 auto flush=[&](){at+=241;input.sample(0,false,at,InputPresentation{});check();};
 auto single=[&](){tap();flush();};auto twice=[&](){tap();tap();flush();};auto triple=[&](){tap();tap();tap();flush();};
 snapshotName="welcome";load("launcher");assert(shown["message"]=="欢迎使用 · 中文");tap();assert(shown["message"]=="欢迎使用 · 中文");flush();assert(shown["message"]=="Welcome · English");snapshotName="network-ap";twice();assert(ap&&shown["message"]=="http://192.168.4.1/");snapshotName="home";twice();assert(shown["clock"]=="13:28"&&shown["power"]=="75%");
 // Double-click opens the original visible item, with no provisional movement.
 auto original=std::string(shown["message"]|"");twice();assert(launches==1&&launched=="voice"&&shown["message"]==original);
 load("launcher");single();assert(strstr(shown["message"]|"","Installed apps"));twice();assert(shown["message"]=="voice");triple();assert(shown["message"]=="voice"&&launches==1);single();assert(shown["message"]=="timer");twice();assert(launches==2&&launched=="timer");
 load("launcher");single();twice();single();single();single();assert(strstr(shown["message"]|"","Next page"));twice();assert(shown["message"]=="clock");single();single();assert(strstr(shown["message"]|"","Previous page"));twice();assert(shown["message"]=="voice");
 load("launcher");single();single();twice();assert(shown["title"]=="Network and settings");wifiOnline=true;snapshotName="network-lan";{Event e;strcpy(e.path,"/dev/wifi");strcpy(e.name,"connected");strcpy(e.data,"{}");JsonDocument d;core.emit(e.path,e.name,d.to<JsonObject>());e.sequence=core.sequence();host.event(e);check();assert(shown["message"]=="http://192.0.2.20/"&&shown["ssid"]=="Home WiFi");}twice();wifiOnline=false;
 single();single();single();twice();assert(shown["title"]=="Controls & Home"&&shown["page"]=="1 / 5");snapshotName="help";single();assert(shown["page"]=="2 / 5");single();single();single();assert(shown["page"]=="5 / 5");twice();assert(shown["title"]=="WishOS");
 // A long physical hold cancels the pending short click in the native detector.
 original=shown["message"]|"";tap();{auto s=presentationState.presented();InputPresentation stamp{s.view,s.revision,s.activation};input.sample(0,true,at,stamp);input.sample(0,true,at+26,stamp);input.sample(0,true,at+850,stamp);input.sample(0,false,at+900,stamp);input.sample(0,false,at+926,stamp);at+=1000;}flush();assert(shown["message"]==original);
 // A real clock timer may redraw while a single click is pending.
 tap();timeText="2026-10-17T13:29:00+0800";{JsonDocument timers;core.listTimers(timers["timers"].to<JsonArray>());assert(timers["timers"].size()==1);JsonDocument d;d["id"]=timers["timers"][0]["id"];Event e;strcpy(e.path,"/sys/timers");strcpy(e.name,"fired");serializeJson(d,e.data,sizeof e.data);core.emit(e.path,e.name,d.as<JsonObjectConst>());e.sequence=core.sequence();host.event(e);}assert(shown["clock"]=="13:29");flush();assert(strstr(shown["message"]|"","Installed apps"));

 // A replaced menu's stale click does not trigger anything in the voice page.
 load("launcher");tap();snapshotName="voice";load("voice");flush();assert(micStarts==0&&shown["message"]=="Hold to speak; release to send");
 auto event=[&](const char*path,const char*name,JsonDocument&data){Event e;copy(e.path,sizeof e.path,path);copy(e.name,sizeof e.name,name);assert(core.emit(e.path,e.name,data.as<JsonObjectConst>()));e.sequence=core.sequence();serializeJson(data,e.data,sizeof e.data);host.event(e);host.tick(core.now());check();};
 auto deliver=[&](const char*name,JsonDocument&data){event("/services/ai",name,data);};
 auto reply=[&](const char*op,const char*payload){uint32_t id=aiId;JsonDocument action,done;action["id"]=id;action["step"]=0;action["topic"]="session_control";JsonDocument body;assert(!deserializeJson(body,payload));action["data"].set(body);auto data=action["data"].as<JsonObject>();data["format"]="wishos-session-control/1";data["operation"]=op;data["request_id"]="fixture:"+std::to_string(id);data["ok"]=true;deliver("application_action",action);done["id"]=id;deliver("completed",done);};
 // Opening a first conversation resolves/creates it without recording. Double
 // click opens a menu choice; it never duplicates protected hold-to-speak.
 linkOnline=true;load("voice");assert(aiOperation=="status"&&micStarts==0);
 reply("status",R"({"operations":["status","create","list","select"]})");assert(aiOperation=="create"&&micStarts==0);
 reply("create",R"({"selected_session":{"runtime_id":"fixture","session_id":"first","session_scope":"local"},"session":{"title":"First conversation"}})");
 assert(micStarts==0&&shown["detail"]=="Chats"&&shown["previous_choice"]=="Home"&&shown["next_choice"]=="Switch reply");
 snapshotName="voice-menu";single();assert(shown["detail"]=="Switch reply");twice();assert(shown["output_label"]=="Voice reply"&&micStarts==0);twice();assert(shown["output_label"]=="Text reply");single();assert(shown["detail"]=="Home");single();assert(shown["detail"]=="Chats");twice();assert(aiOperation=="status");
 reply("status",R"({"operations":["status","list","select","create"]})");assert(aiOperation=="list");snapshotName="sessions";reply("list",R"({"entries":[{"title":"Saved conversation","reference":{"runtime_id":"fixture","session_id":"one","session_scope":"local"}}],"next_cursor":"page-two"})");assert(shown["message"]=="Saved conversation");single();assert(shown["message"]=="Next");twice();reply("list",R"({"entries":[{"title":"Other conversation","reference":{"runtime_id":"fixture","session_id":"two","session_scope":"local"}}]})");single();assert(shown["message"]=="Previous");twice();reply("list",R"({"entries":[{"title":"Saved conversation","reference":{"runtime_id":"fixture","session_id":"one","session_scope":"local"}}]})");twice();assert(aiOperation=="select");reply("select",R"({"selected_session":{"runtime_id":"fixture","session_id":"one","session_scope":"local"},"session":{"title":"Saved conversation"}})");assert(shown["session_title"]=="Saved conversation"&&micStarts==0);
 auto route=[&](const char*path,const char*name,uint32_t id){JsonDocument d;d["request_id"]=id;event(path,name,d);};
 requestSequence=77;route("/sys/apps","voice_requested",77);assert(micStarts==0);requestHeld=true;route("/sys/apps","voice_requested",77);assert(micStarts==1);route("/sys/navigation","voice_released",76);assert(micStops==0);requestHeld=false;route("/sys/navigation","voice_released",77);assert(micStops==1);
 {JsonDocument e;e["recording_id"]="fixture-recording";event("/dev/mic","stopped",e);}
 {JsonDocument a,done;a["id"]=aiId;a["step"]=0;a["data"]["text"]="abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";deliver("application_action",a);done["id"]=aiId;deliver("completed",done);}
 auto first=std::string(shown["message"]|"");single();single();twice();assert(first!=std::string(shown["message"]|""));single();twice();assert(first==std::string(shown["message"]|""));single();twice();assert(homes==1);
 // Release before an asynchronous first-session setup finishes clears recording
 // intent. The newly created reference remains useful for a later real hold.
 {JsonDocument out;host.invoke("stop",JsonObjectConst(),out.to<JsonObject>());size_t wrote;const char*preferences=R"({"language":"en","onboarded":true})";assert(!factory.write("/preferences.json",0,(const uint8_t*)preferences,strlen(preferences),true,wrote));requestHeld=true;requestSequence=92;auto starts=micStarts;
  load("voice");assert(aiOperation=="status");requestHeld=false;route("/sys/navigation","voice_released",92);
  reply("status",R"({"operations":["status","create"]})");reply("create",R"({"selected_session":{"runtime_id":"fixture","session_id":"released","session_scope":"local"}})");assert(micStarts==starts);
 }
 // The Chinese help pages use their own readable paragraph layout too.
 {JsonDocument a,o;host.invoke("stop",a.to<JsonObject>(),o.to<JsonObject>());size_t wrote;const char*pref=R"({"language":"zh","onboarded":true})";assert(!factory.write("/preferences.json",0,(const uint8_t*)pref,strlen(pref),true,wrote));load("launcher");single();single();single();twice();assert(shown["page"]=="1 / 5");for(unsigned i=0;i<4;++i){snapshotName=i==0?"help-zh-2":i==1?"help-zh-3":i==2?"help-zh-4":"help-zh-5";single();}assert(shown["page"]=="5 / 5");snapshotName="home-zh";twice();assert(shown["title"]=="WishOS");}
 contentValid=false;uint32_t bootCursor=core.sequence();core.setEventObserver([](const Event&e){return runtime->event(e);});snapshotName="boot-logo";load("launcher");assert(strstr(uiCurrent,"splash.json"));for(unsigned i=0;i<35;++i){core.clock(core.now()+20);core.tick();core.dispatch(bootCursor);host.tick(core.now());check();}assert(strstr(uiCurrent,"home.json")&&contentValid);load("launcher");assert(strstr(uiCurrent,"home.json"));contentValid=false;failSplash=true;bootCursor=core.sequence();load("launcher");core.dispatch(bootCursor);host.tick(core.now());check();assert(strstr(uiCurrent,"home.json"));failSplash=false;core.setEventObserver(nullptr);
 JsonDocument o;assert(!host.invoke("stop",JsonObjectConst(),o.to<JsonObject>()));assert(core.resources().promised()==0);
 assert(suiteLayoutErrors.empty());resetUiRenderer();std::filesystem::remove_all(temporary);std::cout<<"Square LCD single-button launcher, AP configuration, app pagination, saved preferences, offline voice and typed session selection passed\n";
}
