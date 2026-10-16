#include "Core.h"
#include "NativeOperation.h"
#include "BleConfiguration.h"
#include "WireBytes.h"
#include "host/Arduino.h"
#include <thread>
#include <atomic>
#include <vector>
#include <map>
#include <cassert>
#include <iostream>
using namespace notesos;
static std::thread task;static bool launchFailure=false;static std::atomic<bool>hold{false},entered{false};static unsigned failure=0,allocations=0,deinitCount=0;static bool initialized=false;
static bool fail(){return ++allocations==failure;}
constexpr int pdPASS=1,HID_KEYBOARD=1;
static int xTaskCreate(void(*f)(void*),const char*,unsigned,void*p,unsigned,void*){if(launchFailure)return 0;task=std::thread(f,p);return 1;}static void vTaskDelete(void*){}
struct BLEUUID{String value;BLEUUID(const char*s="0001"):value(s){}String toString(){return value;}};
struct BLEAddress{BLEAddress(const char*){}String toString(){return "00:11:22:33:44:55";}};
struct BLECharacteristic;struct BLECharacteristicCallbacks{virtual~BLECharacteristicCallbacks()=default;virtual void onWrite(BLECharacteristic*){}};
struct BLECharacteristic{BLEUUID getUUID(){return {};}String getValue(){return "x";}void setCallbacks(BLECharacteristicCallbacks*){}void setValue(uint8_t*,size_t){}void notify(){}};
struct BLERemoteCharacteristic{String readValue(){return "x";}bool writeValue(uint8_t*,size_t,bool){return true;}bool subscribe(bool,void(*)(BLERemoteCharacteristic*,uint8_t*,size_t,bool),bool){return true;}};
struct BLERemoteService{BLERemoteCharacteristic c;BLERemoteCharacteristic*getCharacteristic(const char*){return &c;}};
struct BLEClient{BLERemoteService service;bool connect(BLEAddress,uint8_t,uint32_t){return true;}bool isConnected(){return true;}BLERemoteService*getService(const char*){return &service;}};
struct BLEService{std::vector<BLECharacteristic*>chars;~BLEService(){for(auto p:chars)delete p;}BLECharacteristic*createCharacteristic(BLEUUID,unsigned){if(fail())return nullptr;auto p=new BLECharacteristic;chars.push_back(p);return p;}void start(){}BLEUUID getUUID(){return {};}};
struct BLEServer{BLEService*service=nullptr;~BLEServer(){delete service;}void advertiseOnDisconnect(bool){}unsigned getConnectedCount(){return 0;}std::map<int,int>getPeerDevices(bool){return {};}uint16_t getPeerMTU(int){return 23;}BLEService*createService(BLEUUID){if(fail())return nullptr;service=new BLEService;return service;}void start(){}};
struct BLEAdvertisedDevice{BLEAddress getAddress(){return BLEAddress("");}int getAddressType(){return 0;}int getRSSI(){return -30;}String getName(){return "peer";}};
struct BLEAdvertisedDeviceCallbacks{virtual~BLEAdvertisedDeviceCallbacks()=default;virtual void onResult(BLEAdvertisedDevice){}};
struct BLEScanResults{int getCount(){return 0;}BLEAdvertisedDevice getDevice(int){return {};}};
struct BLEScan{BLEScanResults results;void clearResults(){}void setAdvertisedDeviceCallbacks(BLEAdvertisedDeviceCallbacks*,bool){}void setActiveScan(bool){}bool start(uint32_t,void(*)(BLEScanResults)){return true;}bool stop(){return true;}BLEScanResults*getResults(){return &results;}};
struct BLEAdvertising{void setAppearance(int){}void addServiceUUID(BLEUUID){}};
struct BLEDevice{static inline BLEServer*server=nullptr;static inline BLEClient*client=nullptr;static inline BLEScan scan;static inline BLEAdvertising advertising;
 static bool init(const char*){entered=true;while(hold)std::this_thread::yield();if(fail())return false;initialized=true;return true;}static BLEClient*createClient(){if(fail())return nullptr;return client=new BLEClient;}static BLEServer*createServer(){if(fail())return nullptr;return server=new BLEServer;}static void stopAdvertising(){}static void startAdvertising(){}static BLEAdvertising*getAdvertising(){return &advertising;}static BLEScan*getScan(){return &scan;}static void deinit(bool){delete server;delete client;server=nullptr;client=nullptr;initialized=false;++deinitCount;}};
struct BLEHIDDevice{BLEService service;BLECharacteristic reports[5];BLEHIDDevice(BLEServer*){}void manufacturer(const char*){}void hidInfo(int,int){}void reportMap(uint8_t*,size_t){}BLECharacteristic*inputReport(unsigned i){return fail()?nullptr:&reports[i-1];}BLECharacteristic*outputReport(unsigned){return fail()?nullptr:&reports[4];}void startServices(){}BLEService*hidService(){return &service;}};
struct BLESecurity{static void setAuthenticationMode(bool,bool,bool){}static void setCapability(int){}};
struct Preferences{size_t putString(const char*,const char*s){return strlen(s);}String getString(const char*,const char*s){return s;}};
static Core core;static Preferences preferences;static void event(const char*p,const char*e){JsonDocument d;core.emit(p,e,d.as<JsonObjectConst>());}static void emit(const char*p,const char*e,JsonObjectConst d,uint32_t owner=0){core.emit(p,e,d,owner);}
#include "drivers/esp32-services/BleService.h"
static void wait(){do{pollBle();std::this_thread::yield();}while(bleLifecycle.active()||!bleLifecycleWorker.idle());pollBle();}
int main(){JsonDocument a,o;core.add({"/dev/ble","start,restart,stop,read","",bleHandler,[](JsonObject o){for(auto m:{"start","restart","stop"})NativeOperation::describe(o,m,"/dev/ble");},"read"});a["mode"]="central";
 hold=true;entered=false;assert(!core.request("/dev/ble","start",a.as<JsonObjectConst>(),o.to<JsonObject>()));auto receipt=o["completion_id"].as<uint32_t>();while(!entered)std::this_thread::yield();assert(!bleHandler("read",JsonObjectConst(),o.to<JsonObject>())&&o["operation_active"]==true);assert(bleHandler("restart",a.as<JsonObjectConst>(),o.to<JsonObject>()));hold=false;wait();assert(bleRunning&&initialized);assert(!core.completions().read(receipt,0,o.to<JsonObject>())&&o["event"]=="operation_completed");assert(!core.completions().release(receipt,0));assert(!bleHandler("stop",JsonObjectConst(),o.to<JsonObject>()));wait();assert(!bleRunning&&!initialized&&core.resources().promised()==8192);
 // Teardown must remain executable at the current resource ceiling: it reuses
 // its retained native stack rather than admitting a second worker.
 a.clear();a["mode"]="central";assert(!bleHandler("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));wait();JsonDocument budget;budget["memory_limit_bytes"]=core.resources().promised();assert(!core.resources().configure(budget.as<JsonObjectConst>()));assert(!bleHandler("stop",JsonObjectConst(),o.to<JsonObject>()));wait();assert(!bleRunning&&!initialized);budget["memory_limit_bytes"]=nullptr;assert(!core.resources().configure(budget.as<JsonObjectConst>()));
 // Fail every allocator-visible boundary of GATT construction. Successful
 // admission must still terminate with a failed receipt and no live SDK objects.
 a.clear();a["mode"]="gatt";a["service_uuid"]="1234";a["characteristics"][0]["uuid"]="1235";a["characteristics"][0]["properties"]=10;
 for(failure=1;failure<=4;++failure){allocations=0;assert(!core.request("/dev/ble","start",a.as<JsonObjectConst>(),o.to<JsonObject>()));receipt=o["completion_id"];wait();assert(!bleRunning&&!initialized);assert(!core.completions().read(receipt,0,o.to<JsonObject>())&&o["event"]=="operation_failed");assert(!core.completions().release(receipt,0));assert(core.resources().promised()==8192);}
 bleLifecycleWorker.shutdown();task.join();assert(core.resources().promised()==0);failure=0;launchFailure=true;assert(bleHandler("start",a.as<JsonObjectConst>(),o.to<JsonObject>()));assert(!bleOperation&&!initialized&&core.resources().promised()==0);
 std::cout<<"BLE SDK lifecycle is nonblocking; failed construction/launch is cleaned and completion is correlated\n";
}
