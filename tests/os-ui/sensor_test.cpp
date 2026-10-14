#include "drivers/gxhtc3/Gxhtc3.h"
#include "drivers/qmc5883l/Qmc5883l.h"
#include "drivers/qmi8658/Qmi8658.h"
#include "drivers/ft6336/Ft6336.h"
#include "drivers/pca9557/Pca9557.h"
#include "ScriptHost.h"
#include <cassert>
#include <cstring>
#include <cmath>
#include <vector>
#include <iostream>
using namespace notesos;using namespace notesos::drivers;
struct BusFixture {
 bool held=false,busy=false;unsigned transfers=0,locks=0,unlocks=0;const char*failure=nullptr;
 uint8_t registers[128][256]{};uint8_t env[6]{};std::vector<uint16_t>commands;
 static bool acquire(void*p){auto&s=*static_cast<BusFixture*>(p);if(s.busy)return false;assert(!s.held);s.held=true;++s.locks;return true;}
 static void release(void*p){auto&s=*static_cast<BusFixture*>(p);assert(s.held);s.held=false;++s.unlocks;}
 static const char*transfer(void*p,uint8_t address,const uint8_t*tx,size_t tn,uint8_t*rx,size_t rn){auto&s=*static_cast<BusFixture*>(p);assert(s.held);++s.transfers;if(s.failure)return s.failure;
  if(address==0x70){if(tn==2)s.commands.push_back(uint16_t(tx[0])<<8|tx[1]);if(rn)memcpy(rx,s.env,rn);return nullptr;}
  if(rn){assert(tn==1);memcpy(rx,s.registers[address]+tx[0],rn);}else if(tn){assert(tn==2);s.registers[address][tx[0]]=tx[1];}return nullptr;
 }
 I2cBus bus{this,acquire,release,transfer};
 void environment(uint16_t temperature,uint16_t humidity){env[0]=temperature>>8;env[1]=temperature;env[2]=crc8(env,2);env[3]=humidity>>8;env[4]=humidity;env[5]=crc8(env+3,2);}
 void put(uint8_t address,uint8_t reg,int16_t value){registers[address][reg]=uint16_t(value)&255;registers[address][reg+1]=uint16_t(value)>>8;}
};
static Core*activeCore;static Sampling*activeSensor;static ScriptHost*activeApp;
int main(){
 BusFixture fixture;Core core;activeCore=&core;Gxhtc3 env(fixture.bus);Sampling sensor(core,env,"/sensors/room");activeSensor=&sensor;
 core.add({"/sensors/room",Sampling::Methods,"",[](const char*m,JsonObjectConst a,JsonObject o){return activeSensor->invoke(m,a,o);},[](JsonObject o){activeSensor->describe(o);},"read,describe"});
 core.add({"/sys/capabilities","describe","",[](const char*,JsonObjectConst a,JsonObject o){return activeCore->describe(a["path"]|"",o);},nullptr,"describe"});
 JsonDocument a,o,empty;uint32_t owner=core.acquireOwner();unsigned before=fixture.transfers;
 assert(!core.invoke("/sensors/room","describe",empty.to<JsonObject>(),o.to<JsonObject>(),owner));assert(!core.invoke("/sensors/room","read",empty.as<JsonObjectConst>(),o.to<JsonObject>(),owner));assert(!o["valid"].as<bool>()&&fixture.transfers==before);
 fixture.environment(26214,32768);core.clock(100);assert(!core.request("/sensors/room","measure",empty.as<JsonObjectConst>(),o.to<JsonObject>(),owner));uint32_t completion=o["completion_id"];
 assert(!fixture.held&&fixture.commands.back()==0x3517);sensor.poll(102);assert(fixture.commands.back()==0x7866);before=fixture.transfers;sensor.poll(103);assert(fixture.transfers==before&&!fixture.held);
 sensor.poll(118);assert(fixture.commands.back()==0xb098);assert(!core.completions().read(completion,owner,o.to<JsonObject>()));assert(o["ready"]==true&&o["event"]=="completed"&&o["data"]["operation_id"]==1);assert(!core.completions().release(completion,owner));
 before=fixture.transfers;assert(!sensor.invoke("read",empty.as<JsonObjectConst>(),o.to<JsonObject>()));assert(fabs(o["temperature_c"].as<double>()-25)<0.01&&o["relative_humidity_pct"]==50);assert(fixture.transfers==before);
 // CRC failure never replaces the last valid reading and still sleeps the sensor.
 fixture.env[2]^=1;core.clock(200);assert(!core.request("/sensors/room","measure",empty.as<JsonObjectConst>(),o.to<JsonObject>(),owner));completion=o["completion_id"];sensor.poll(202);sensor.poll(218);assert(!core.completions().read(completion,owner,o.to<JsonObject>()));assert(o["event"]=="failed"&&o["data"]["error"]=="sensor_crc");assert(!core.completions().release(completion,owner));assert(fixture.commands.back()==0xb098);
 assert(!sensor.invoke("read",empty.as<JsonObjectConst>(),o.to<JsonObject>()));assert(o["sampled_at_ms"]==118);fixture.environment(26214,32768);
 // Real runtime callback, followed by owned cancellation/cleanup.
 Vfs files;ScriptHost app(core,files);activeApp=&app;core.setEventObserver([](const Event&e){return activeApp->event(e);});core.clock(300);a["memory_bytes"]=65536;a["source"]=R"lua(function on_start() assert(notes.request('/sensors/room','measure',{},function(e) assert(e.event=='completed' and e.data.relative_humidity_pct==50); print('sensor callback') end).ok) end)lua";
 if(auto error=app.invoke("load",a.as<JsonObjectConst>(),o.to<JsonObject>())){std::cerr<<error<<" ";serializeJson(o,std::cerr);abort();}auto cursor=core.sequence();sensor.poll(302);sensor.poll(318);core.dispatch(cursor);app.tick(318);assert(!app.invoke("read",empty.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!o["fault"].as<bool>()&&o["callbacks"]==0);assert(!app.invoke("stop",empty.as<JsonObjectConst>(),o.to<JsonObject>()));core.setEventObserver(nullptr);
 core.clock(400);assert(!core.invoke("/sensors/room","measure",empty.as<JsonObjectConst>(),o.to<JsonObject>(),owner));fixture.busy=true;core.releaseOwner(owner);assert(!sensor.invoke("read",empty.as<JsonObjectConst>(),o.to<JsonObject>()));assert(o["cleanup_pending"]==true);fixture.busy=false;sensor.poll(401);assert(fixture.commands.back()==0xb098);assert(!sensor.invoke("read",empty.as<JsonObjectConst>(),o.to<JsonObject>()));assert(o["cleanup_pending"]==false);
 Qmc5883l mag(fixture.bus);a.clear();a["range_gauss"]=4;before=fixture.transfers;assert(mag.configure(a.as<JsonObjectConst>(),0));assert(fixture.transfers==before);a.clear();assert(!mag.configure(a.as<JsonObjectConst>(),0));assert(fixture.registers[0x0d][9]==9);
 fixture.registers[0x0d][6]=1;fixture.put(0x0d,0,12000);fixture.put(0x0d,2,-6000);fixture.put(0x0d,4,0);assert(!mag.begin(0));assert(mag.poll(0).complete);mag.values(o.to<JsonObject>());assert(o["x_ut"]==100&&o["y_ut"]==-50);
 fixture.registers[0x0d][6]=3;assert(!mag.begin(1));assert(!strcmp(mag.poll(1).error,"magnetic_overflow"));
 a.clear();a["enabled"]=false;assert(!mag.configure(a.as<JsonObjectConst>(),2));assert((fixture.registers[0x0d][9]&3)==0);assert(!strcmp(mag.begin(2),"sensor_disabled"));
 Qmi8658 imu(fixture.bus,Qmi8658::Profile::RevA);fixture.registers[0x6a][0]=5;a.clear();assert(!imu.configure(a.as<JsonObjectConst>(),0));assert(fixture.registers[0x6a][3]==0x16&&fixture.registers[0x6a][4]==0x56);assert(!(fixture.registers[0x6a][3]&128)&&!(fixture.registers[0x6a][4]&128));
 assert(!imu.begin(0));before=fixture.transfers;assert(!imu.poll(1).complete&&fixture.transfers==before);fixture.registers[0x6a][0x2e]=3;fixture.put(0x6a,0x33,25*256);fixture.put(0x6a,0x35,8192);fixture.put(0x6a,0x3b,64);assert(imu.poll(200).complete);imu.values(o.to<JsonObject>());assert(o["accel_x_g"]==1&&o["gyro_x_dps"]==1&&o["die_temperature_c"]==25);
 a["odr_hz"]=125;before=fixture.transfers;assert(imu.configure(a.as<JsonObjectConst>(),200));assert(fixture.transfers==before);a["gyro_enabled"]=false;a["odr_hz"]=3;assert(!imu.configure(a.as<JsonObjectConst>(),200));assert(imu.timeoutMs()>1000);
 a.clear();a["enabled"]=false;assert(!imu.configure(a.as<JsonObjectConst>(),300));assert(fixture.registers[0x6a][8]==0&&fixture.registers[0x6a][2]==0x41);assert(!strcmp(imu.begin(301),"sensor_disabled"));
 Ft6336 touch(fixture.bus);fixture.registers[0x38][2]=1;fixture.registers[0x38][3]=0;fixture.registers[0x38][4]=123;fixture.registers[0x38][5]=0x21;fixture.registers[0x38][6]=0xc8;assert(!touch.begin(0));assert(touch.poll(0).complete);touch.values(o.to<JsonObject>());assert(o["contacts"][0]["id"]==2&&o["contacts"][0]["x"]==123&&o["contacts"][0]["y"]==456);
 fixture.registers[0x38][2]=3;assert(!touch.begin(1));assert(touch.poll(1).error);fixture.registers[0x38][2]=0;assert(!touch.begin(2));assert(touch.poll(2).complete);touch.values(o.to<JsonObject>());assert(o["contacts"].size()==0);
 Pca9557 expander(fixture.bus);fixture.registers[0x19][1]=0xf0;fixture.registers[0x19][3]=0xff;assert(!expander.configure(1,1,1));assert(fixture.registers[0x19][1]==0xf1&&fixture.registers[0x19][3]==0xfe);assert(!expander.write(2,2));assert(fixture.registers[0x19][1]==0xf3);
 assert(fixture.locks==fixture.unlocks&&!fixture.held&&core.resources().promised()==0);
 std::cout<<"Optional sensors: runtime completion, nonmutating reads, CRC/error preservation, owner cleanup, bus release during conversion, exact ranges, QMI revision scaling, touch contacts and private expander RMW passed\n";
}
