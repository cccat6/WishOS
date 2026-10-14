// Included in the BSP namespace. GATT/HID are compiled protocols; applications choose their data.
static uint32_t nextBleOperation=1,bleScanId=0,bleScanOwner=0;
static std::atomic<bool>bleScanning{false};static unsigned bleScanCount=0,bleScanLimit=16;
static BLEClient*bleCentral=nullptr;static std::atomic<bool>bleOperation{false};
static BLEServer*bleServer=nullptr;static BLEHIDDevice*bleHid=nullptr;static BLECharacteristic*bleInputs[4]{};
static unsigned bleInputCount=0;static bool bleRunning=false;static uint32_t bleConnections=0;
class BleWriteEvents:public BLECharacteristicCallbacks {
 void onWrite(BLECharacteristic*c)override{auto value=c->getValue();JsonDocument d;d["uuid"]=c->getUUID().toString();
   if(value.length()>128){d["error"]="characteristic_limit";emit("/dev/ble","overflow",d.as<JsonObjectConst>());return;}
   char bytes[257];hex((const uint8_t*)value.c_str(),value.length(),bytes);d["hex"]=bytes;emit("/dev/ble","write",d.as<JsonObjectConst>());}
};
static BleWriteEvents bleCallbacks;
static bool bleUuid(const char*s){size_t n=strlen(s);if(n!=4&&n!=8&&n!=36)return false;for(size_t i=0;i<n;++i)if(n==36&&(i==8||i==13||i==18||i==23)){if(s[i]!='-')return false;}else if(!isxdigit((unsigned char)s[i]))return false;return true;}
struct BleOperation{uint32_t id=0,owner=0,reservation=0;~BleOperation(){if(reservation)core.resources().release(reservation);}char op[16]{},address[18]{},service[40]{},characteristic[40]{};uint8_t bytes[128]{};size_t length=0;uint32_t timeout=10000;uint8_t addressType=0;};
static void bleCentralNotify(BLERemoteCharacteristic*,uint8_t*data,size_t n,bool){JsonDocument d;if(n>128){event("/dev/ble","overflow");return;}char bytes[257];hex(data,n,bytes);d["hex"]=bytes;emit("/dev/ble","notification",d.as<JsonObjectConst>());}
static void bleOperationTask(void*ptr){auto job=(BleOperation*)ptr;JsonDocument d;d["operation"]=job->op;d["operation_id"]=job->id;d["owner"]=job->owner;bool ok=false;
 if(!strcmp(job->op,"connect"))ok=bleCentral->connect(BLEAddress(job->address),job->addressType,job->timeout);
 else if(bleCentral->isConnected()){
   auto service=bleCentral->getService(job->service);auto chr=service?service->getCharacteristic(job->characteristic):nullptr;
   if(chr){if(!strcmp(job->op,"gatt_read")){auto value=chr->readValue();if(value.length()<=128){char bytes[257];hex((const uint8_t*)value.c_str(),value.length(),bytes);d["hex"]=bytes;ok=true;}}
     else if(!strcmp(job->op,"gatt_write"))ok=chr->writeValue(job->bytes,job->length,true);
     else if(!strcmp(job->op,"subscribe"))ok=chr->subscribe(true,bleCentralNotify,true);
   }
 }
 d["ok"]=ok;emit("/dev/ble",ok?"operation_completed":"operation_failed",d.as<JsonObjectConst>(),job->owner);delete job;bleOperation=false;vTaskDelete(nullptr);
}
static void bleScanComplete(BLEScanResults){if(!bleScanning.exchange(false))return;JsonDocument d;d["operation_id"]=bleScanId;d["owner"]=bleScanOwner;emit("/dev/ble","scan_completed",d.as<JsonObjectConst>(),bleScanOwner);}
class BleScanEvents:public BLEAdvertisedDeviceCallbacks {
 void onResult(BLEAdvertisedDevice device)override{JsonDocument d;d["address"]=device.getAddress().toString();d["address_type"]=device.getAddressType();d["rssi"]=device.getRSSI();d["name"]=device.getName().substring(0,63);emit("/dev/ble","discovered",d.as<JsonObjectConst>());if(++bleScanCount>=bleScanLimit)BLEDevice::getScan()->stop();}
};
static BleScanEvents bleScanCallbacks;
static void stopBle(){if(!bleRunning)return;if(bleScanning){BLEDevice::getScan()->stop();bleScanComplete(BLEScanResults{});}BLEDevice::stopAdvertising();delete bleHid;bleHid=nullptr;BLEDevice::deinit(false);bleCentral=nullptr;bleServer=nullptr;bleInputCount=0;bleRunning=false;event("/dev/ble","stopped");}
static uint16_t bleNotifyCapacity(){uint16_t n=128;if(!bleServer||!bleServer->getConnectedCount())return 0;auto peers=bleServer->getPeerDevices(false);if(peers.empty())return 20;
 for(auto&peer:peers){uint16_t mtu=bleServer->getPeerMTU(peer.first);uint16_t capacity=mtu>=23?mtu-3:20;if(capacity<n)n=capacity;}return n;
}
static const char*bleRuntimeHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"scan")){
   if(!bleCentral)return "ble_central_required";if(bleOperation||bleScanning)return "ble_operation_active";
   uint32_t seconds=a["seconds"]|5u,limit=a["limit"]|16u;if(!seconds||seconds>30||!limit||limit>16)return "scan_range";
   auto scanner=BLEDevice::getScan();scanner->clearResults();scanner->setAdvertisedDeviceCallbacks(&bleScanCallbacks,false);scanner->setActiveScan(a["active"]|true);bleScanCount=0;bleScanLimit=limit;bleScanId=nextBleOperation++;bleScanOwner=core.callerOwner();bleScanning=true;
   if(!scanner->start(seconds,bleScanComplete)){bleScanning=false;return "ble_scan_failed";}out["state"]="scanning";out["operation_id"]=bleScanId;return nullptr;
 }
 if(!strcmp(m,"scan_results")){
   if(!bleCentral)return "ble_central_required";if(bleScanning)return "scan_in_progress";auto result=BLEDevice::getScan()->getResults();auto items=out["devices"].to<JsonArray>();
   for(int i=0;i<result->getCount()&&i<16;++i){auto device=result->getDevice(i);auto item=items.add<JsonObject>();item["address"]=device.getAddress().toString();item["address_type"]=device.getAddressType();item["name"]=device.getName();item["rssi"]=device.getRSSI();}return nullptr;
 }
 if(!strcmp(m,"scan_stop")){if(bleScanning){if(core.callerOwner()&&bleScanOwner!=core.callerOwner())return "ble_scan_owned_by_other";if(!BLEDevice::getScan()->stop())return "ble_scan_stop_failed";bleScanComplete(BLEScanResults{});}return nullptr;}
 if(!strcmp(m,"stop")){if(bleOperation)return "ble_operation_active";stopBle();return nullptr;}
 if(!strcmp(m,"start")){
   if(bleRunning)return "ble_already_running";const char*name=a["name"]|"Agentic Notes",*mode=a["mode"]|"gatt";if(!*name||strlen(name)>29)return "ble_name_limit";
   bool hid=!strcmp(mode,"hid"),central=!strcmp(mode,"central");if(!hid&&!central&&strcmp(mode,"gatt"))return "ble_mode";
   uint8_t descriptor[512];size_t length=0;auto chars=a["characteristics"].as<JsonArrayConst>();const char*service=a["service_uuid"]|"";
   if(hid){if(!unhex(a["report_map_hex"]|"",descriptor,sizeof descriptor,length)||!length)return "hid_report_map_required";}
   else if(!central){if(!bleUuid(service)||!chars.size()||chars.size()>4)return "gatt_configuration";for(auto c:chars)if(!bleUuid(c["uuid"]|"")||((c["properties"]|0u)&~26u)||(c["properties"]|0u)==0)return "gatt_characteristic";}
   if(!BLEDevice::init(name))return "ble_init_failed";BLESecurity::setAuthenticationMode(true,false,true);BLESecurity::setCapability(3);if(central){bleCentral=BLEDevice::createClient();bleRunning=true;out["state"]="started";return nullptr;}bleServer=BLEDevice::createServer();bleServer->advertiseOnDisconnect(true);
   if(hid){bleHid=new(std::nothrow) BLEHIDDevice(bleServer);if(!bleHid){BLEDevice::deinit(false);bleServer=nullptr;return "memory_low";}bleHid->manufacturer("Agentic Notes");bleHid->hidInfo(0,1);bleHid->reportMap(descriptor,length);
     for(unsigned id=1;id<=4;++id)bleInputs[bleInputCount++]=bleHid->inputReport(id);bleHid->outputReport(1)->setCallbacks(&bleCallbacks);bleHid->startServices();BLEDevice::getAdvertising()->setAppearance(HID_KEYBOARD);BLEDevice::getAdvertising()->addServiceUUID(bleHid->hidService()->getUUID());}
   else{auto svc=bleServer->createService(BLEUUID(service));for(auto c:chars){auto chr=svc->createCharacteristic(BLEUUID(c["uuid"].as<const char*>()),c["properties"].as<uint32_t>());chr->setCallbacks(&bleCallbacks);bleInputs[bleInputCount++]=chr;}svc->start();bleServer->start();BLEDevice::getAdvertising()->addServiceUUID(svc->getUUID());}
   BLEDevice::startAdvertising();bleRunning=true;event("/dev/ble","started");
 }else if(!strcmp(m,"connect")||!strcmp(m,"gatt_read")||!strcmp(m,"gatt_write")||!strcmp(m,"subscribe")){
   if(!bleCentral)return "ble_central_required";if(bleOperation)return "ble_operation_active";
   const char*addr=a["address"]|"",*svc=a["service_uuid"]|"",*chr=a["characteristic_uuid"]|"";
   if(!strcmp(m,"connect")){if(strlen(addr)!=17)return "ble_address";for(int i=0;i<17;++i)if(i%3==2?addr[i]!=':':!isxdigit((unsigned char)addr[i]))return "ble_address";}
   else if(!bleUuid(svc)||!bleUuid(chr))return "ble_uuid";
   uint32_t timeout=a["timeout_ms"]|10000u,type=a["address_type"]|0u;if(timeout<100||timeout>30000||type>1)return "ble_connection_range";
   auto job=new(std::nothrow) BleOperation;if(!job)return "memory_low";strcpy(job->op,m);if(!strcmp(m,"connect"))strcpy(job->address,addr);else{strcpy(job->service,svc);strcpy(job->characteristic,chr);}job->timeout=timeout;job->addressType=type;
   if(!strcmp(m,"gatt_write")&&!unhex(a["hex"]|"",job->bytes,sizeof job->bytes,job->length)){delete job;return "ble_data_limit";}
   job->reservation=core.resources().acquire(sizeof(BleOperation)+8192,core.callerOwner());if(!job->reservation){delete job;return "ble_memory_budget";}job->id=nextBleOperation++;job->owner=core.callerOwner();auto operationId=job->id;
   bleOperation=true;if(xTaskCreate(bleOperationTask,"notes-ble",6144,job,1,nullptr)!=pdPASS){bleOperation=false;delete job;return "task_allocation_failed";}out["state"]="accepted";out["operation_id"]=operationId;return nullptr;
 }else if(!strcmp(m,"write")){
   unsigned slot=a["index"]|0u;if(!bleRunning||slot>=bleInputCount)return "ble_characteristic_missing";
   uint8_t bytes[128];size_t n;if(!unhex(a["hex"]|"",bytes,sizeof bytes,n))return "ble_data_limit";
   bool notify=a["notify"]|true;if(notify){auto capacity=bleNotifyCapacity();out["max_notify_bytes"]=capacity;if(!capacity)return "ble_not_connected";if(n>capacity)return "ble_notification_mtu_limit";}
   bleInputs[slot]->setValue(bytes,n);if(notify)bleInputs[slot]->notify();out["bytes"]=n;return nullptr;
 }else if(strcmp(m,"read"))return "unsupported_method";
 out["running"]=bleRunning;out["connections"]=bleCentral?unsigned(bleCentral->isConnected()):bleRunning?bleServer->getConnectedCount():0;out["characteristics"]=bleInputCount;out["transport"]="ble";out["mode"]=bleCentral?"central":bleHid?"hid":"gatt";out["operation_active"]=bleOperation.load();out["scanning"]=bleScanning.load();out["max_notify_bytes"]=bleNotifyCapacity();return nullptr;
}
static void pollBle(){if(!bleRunning)return;auto n=bleCentral?unsigned(bleCentral->isConnected()):bleServer->getConnectedCount();if(n!=bleConnections){bleConnections=n;JsonDocument d;d["connections"]=n;emit("/dev/ble","connections",d.as<JsonObjectConst>());}}

static uint32_t bleActiveConfigRevision=0;
static BleConfiguration bleConfiguration;static JsonDocument bleActiveProfile;static char bleBootError[64]{};
static bool persistBleConfiguration(const char*data,size_t n){return preferences.putString("ble_config",data)==n;}
static const char*bleHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"configure"))return bleConfiguration.configure(a,out,persistBleConfiguration);
 if(!strcmp(m,"start")||!strcmp(m,"restart")){
   JsonDocument profile;if(auto e=mergeBleProfile(bleConfiguration.profile(),a,profile.to<JsonObject>()))return e;
   if(!strcmp(m,"restart")){if(bleOperation)return "ble_operation_active";stopBle();}
   auto e=bleRuntimeHandler("start",profile.as<JsonObjectConst>(),out);if(!e){bleActiveProfile.set(profile);bleActiveConfigRevision=bleConfiguration.profileRevision();}else if(!bleRunning)bleActiveProfile.clear();return e;
 }
 auto e=bleRuntimeHandler(m,a,out);if(!e&&!strcmp(m,"stop"))bleActiveProfile.clear();if(!e&&!strcmp(m,"read")){
   bleConfiguration.read(out);out["active_profile"]=bleActiveProfile.as<JsonVariantConst>();out["restart_required"]=bleRunning&&bleActiveConfigRevision!=bleConfiguration.profileRevision()&&bleConfiguration.differs(bleActiveProfile.as<JsonObjectConst>());out["boot_start_error"]=bleBootError;
 }return e;
}
static void restoreBleConfiguration(){
 String saved=preferences.getString("ble_config","");if(saved.length()&&!bleConfiguration.restore(saved.c_str())){copy(bleBootError,sizeof bleBootError,"invalid_saved_configuration");return;}
 if(bleConfiguration.autoStart()){JsonDocument a,out;if(auto e=bleHandler("start",a.as<JsonObjectConst>(),out.to<JsonObject>()))copy(bleBootError,sizeof bleBootError,e);}
}
