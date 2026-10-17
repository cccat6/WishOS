#pragma once
#include "drivers/i2c/RegisterBus.h"
#include "drivers/sensor-sampling/Sampling.h"
namespace notesos::drivers {
class Ft6336 final:public SampleBackend {
public:
 struct Contact{uint16_t x=0,y=0,rawX=0,rawY=0;uint8_t id=0,event=0,weight=0,area=0;};
 struct Frame{uint8_t count=0;Contact contacts[2]{};};
 struct Geometry{unsigned nativeWidth,nativeHeight;bool swapXY,mirrorX,mirrorY;};
private:
 I2cBus&bus_;uint8_t address_;Geometry geometry_;bool pending_=false;uint8_t count_=0;struct Point{uint16_t x,y;uint8_t id,event,weight,area;}points_[2]{};
public:
 explicit Ft6336(I2cBus&bus,uint8_t address=0x38,Geometry geometry={0,0,false,false,false}):bus_(bus),address_(address),geometry_(geometry){}
 uint32_t timeoutMs()const override{return 100;}
 void describe(JsonObject o)const override{o["model"]="FT6336";o["transport"]="I2C";o["address"]=address_;o["max_contacts"]=2;o["coordinates"]=geometry_.nativeWidth?"device display coordinates":"native sensor coordinates";if(geometry_.nativeWidth){o["width"]=geometry_.swapXY?geometry_.nativeHeight:geometry_.nativeWidth;o["height"]=geometry_.swapXY?geometry_.nativeWidth:geometry_.nativeHeight;}o["raw_coordinates"]=true;o["gestures"]="runtime application";auto p=o["properties"]["count"].to<JsonObject>();p["type"]="integer";p["access"]="r";}
 const char*configure(JsonObjectConst a,uint64_t)override{return a.size()?"unknown_sensor_setting":nullptr;}
 const char*begin(uint64_t)override{pending_=true;return nullptr;}
 SampleStep poll(uint64_t)override{if(!pending_)return {};I2cBus::Transaction t(bus_);if(!t.held)return {};uint8_t data[13];if(auto e=t.read(address_,2,data,sizeof data))return {false,e};unsigned count=data[0]&15;if(count>2)return {false,"touch_packet"};Point candidate[2]{};for(unsigned i=0;i<count;++i){auto p=data+1+i*6;candidate[i]={uint16_t((p[0]&15)<<8|p[1]),uint16_t((p[2]&15)<<8|p[3]),uint8_t(p[2]>>4),uint8_t(p[0]>>6),p[4],uint8_t(p[5]>>4)};if(candidate[i].id==15)return {false,"touch_packet"};if(geometry_.nativeWidth&&(candidate[i].x>=geometry_.nativeWidth||candidate[i].y>=geometry_.nativeHeight))return {false,"touch_coordinate"};}for(unsigned i=0;i<count;++i)points_[i]=candidate[i];count_=count;pending_=false;return {true,nullptr};}
 const char*cancel()override{pending_=false;return nullptr;}
 Frame frame()const{Frame out;out.count=count_;for(unsigned i=0;i<count_;++i){auto v=points_[i];unsigned x=geometry_.swapXY?v.y:v.x,y=geometry_.swapXY?v.x:v.y,w=geometry_.swapXY?geometry_.nativeHeight:geometry_.nativeWidth,h=geometry_.swapXY?geometry_.nativeWidth:geometry_.nativeHeight;if(geometry_.mirrorX)x=w-1-x;if(geometry_.mirrorY)y=h-1-y;out.contacts[i]={uint16_t(x),uint16_t(y),v.x,v.y,v.id,v.event,v.weight,v.area};}return out;}
 static void values(const Frame&frame,JsonObject o){o["count"]=frame.count;auto list=o["contacts"].to<JsonArray>();for(unsigned i=0;i<frame.count;++i){auto p=list.add<JsonObject>();auto v=frame.contacts[i];p["id"]=v.id;p["raw_x"]=v.rawX;p["raw_y"]=v.rawY;p["x"]=v.x;p["y"]=v.y;p["event"]=v.event;p["weight"]=v.weight;p["area"]=v.area;}}
 void values(JsonObject o)const override{values(frame(),o);}

};
}
