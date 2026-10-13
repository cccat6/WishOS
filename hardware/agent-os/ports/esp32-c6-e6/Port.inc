#include "../DisplayScheduler.h"
#include "PackedFont.h"
#include <lvgl_private.h>
#include "../Hardware.h"
#include "../ButtonGestures.h"
#include "../MotionInput.h"
#include "../TimeSync.h"
#include <esp_sntp.h>
#include "../Core.h"
#include "../EspMemory.h"
#include "../RpcEnvelope.h"
#include "../PresentationState.h"
#include "../Conditions.h"
#include "../ScriptHost.h"
#include "../RecordingRetention.h"
#include "../VoiceCodec.h"
#include "../ConnectionProfiles.h"
#include "../Workers.h"
#include "../Pins.h"
#include "../PowerSense.h"
#include "../PowerMonitor.h"
#include "../drivers/e6/E6Driver.h"
#include "../drivers/e6/E6Profiles.h"
#include "../Defaults.h"
#include "../DriverRequests.h"
#include "../WifiManager.h"
#include "../BleConfiguration.h"
#include <esp_mac.h>
#include "../SdClockPlan.h"
#include "../SdConfiguration.h"
#include "../SdProvider.h"
#include "../CachePath.h"
#include "../NfsService.h"
#include "../RemoteRequests.h"
#include "../SocketService.h"
#include "../DatagramService.h"
#include "../HttpApplications.h"
#include <WebSocketsClient.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <OThread.h>
#include "../radio/RadioMux.h"
#include <openthread/dataset.h>
#include <Zigbee.h>
#include <transports/SocketTransport.h>
#include "../SystemFiles.h"
#include "../display/Content.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <SdFat.h>
#include "../BoardSdSpi.h"
#include <lvgl.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <Wire.h>
#include <atomic>
#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/rtc_io.h>
#include <esp32-hal-cpu.h>
#include <esp32-hal-spi.h>
#include <esp_private/esp_clk.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <mbedtls/base64.h>
#include <mbedtls/sha256.h>
#include <sys/time.h>
namespace notesos {
static Core core;static const char*resourceStartupError=nullptr;static RemoteRequests remoteRequests(core);static SocketService tcpService(core);static DatagramService udpService(core);static HttpApplications httpApps(core);static Conditions conditions(core);static char appScope[48]="system";
static WebServer http(80);static DNSServer dns;
static SdFs SD;
static BoardSdSpi boardSdSpi(SPI);
static Policies policies(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));
static bool policyRestoreOk=true;
#include "../DriverDescriptions.h"
static Preferences preferences;static ConnectionProfiles connections;
static Vfs vfs;static esp32_nfs::SocketTransport nfsTransport;static NfsService nfsService(vfs,core.resources());static SystemFiles systemFiles;
static void* allocateRAM(size_t n){return core.resources().allocate(n,ResourceBudget::Region::Internal);}
static void freeMemoryFile(void*p){core.resources().freeAllocation(p);}
static MemoryFiles ramFiles("ram","/storage/ram","ram",true,allocateRAM,freeMemoryFile);
static void* allocatePSRAM(size_t n){return core.resources().allocate(n,ResourceBudget::Region::External);}
static MemoryFiles psramFiles("psram","/storage/psram","psram",false,allocatePSRAM,freeMemoryFile);
static bool panelReady();static void panelReset(bool);
static bool panelCommand(uint8_t,const uint8_t*,size_t);static bool panelData(const uint8_t*,size_t);
static E6Driver panel({panelReady,panelReset,panelCommand,panelData});
static E6ColorDriver colorPanel({panelReady,panelReset,panelCommand,panelData});
static E6MonoCache monoCache(core.resources());
static bool spiStarted=false;
static SemaphoreHandle_t spiLease;
static QueueHandle_t eventQueue, audioQueue, spoolQueue;
struct PendingEvent {
  char path[48], name[40], data[MAX_EVENT];
  uint32_t targetOwner=0;
};
struct AudioChunk {
  uint32_t sequence;
  uint16_t samples,bytes;
  uint64_t sampleStart;
  uint8_t data[91];
};
static std::atomic<bool> audioCaptureFailed{false};
static std::atomic<uint32_t> eventDrops{0}, audioDrops{0}, audioStorageDrops{0};
static std::atomic<bool> audioSpooling{false};
static std::atomic<const char*> audioStorageError{"none"};
static bool audioReadableNotified=false;static std::atomic<uint64_t> producedSamples{0};
static bool audioToDisk=false,audioToNetwork=true,audioStopOnOverflow=false,audioStopOnStorageError=false;
static char recordingId[40]{},recordingFile[192]{},recordingRetention[16]{};
static uint32_t nextRecording=1,micOwner=0;
RTC_DATA_ATTR static uint64_t lastRefreshRTC = 0;

static char apSsid[40];

static uint32_t ruleCursor = 0;
static char bootId[17];
static bool rail = false, mounted = false, usbEvents = false, imuFound = false, cardProbed=false, sdTuned=false;
static bool sdCanWrite=true, sdVolumeAttempted=false;
static void sdSetWritable(bool);
static void sdVolumeSnapshot(bool);
static SdLinkState sdLink;
static uint32_t panelSpiHz=16000000;
static uint8_t imuAddress = 0x19;
static std::atomic<bool> audioRunning{false}, audioStop{false}, streaming{false};
static bool displayActive();
static bool displayInputBusy();
static char displayError[64] = "none", displayState[24] = "idle", frameHash[65]{};
static uint32_t streamId = 0, nextStream = 1, streamDeadline = 0, lastClear = 0;
static bool frameValid = false;
static uint8_t frameProfile = 0xff;
static void applyPanelClockIfIdle(){
  if(!displayActive()&&!streaming&&panel.safeToIdle()&&colorPanel.safeToIdle())
    panelSpiHz=policies.get<uint32_t>("/sys/bus","display_spi_hz",16000000);
}
static mbedtls_sha256_context digest;
static uint32_t micRate = 16000, micLimit = 300000, audioSequence = 0;
static uint64_t micStarted = 0;
static uint32_t nextJob = 1;static std::atomic<uint32_t> presentedJob{0};
struct JobReceipt {
  uint32_t id = 0;
  bool complete = false, ok = false;
  uint32_t total = 0;
  char state[24] = "queued";
  char error[64]{};
};
static JobReceipt jobs[8];
static void rememberJob(uint32_t id) {
  jobs[id % 8] = {};
  jobs[id % 8].id = id;
}


static char pendingPower[16]{};
static const char *operatingMode = "normal";
static std::atomic<bool> shutdownReady{false};
static std::atomic<bool> shutdownRequested{false};
static void pollDisplay();
static bool networkActive();
static bool applicationNetworkActive();
static uint32_t powerAt = 0;
static float batteryGain = 1.0f;
struct Wake {
  uint8_t buttons = 7;
  bool imu = false, busy = false;
  uint32_t timerMs = 60000;
};
RTC_DATA_ATTR static Wake wake;
static SemaphoreHandle_t inputMutex, i2cLease;
static bool lastImu = false, lastBusy = false;
static uint64_t uptime() {
  return uint64_t(esp_timer_get_time()) / 1000;
}
static void emit(const char *path, const char *event, JsonObjectConst data,uint32_t targetOwner=0) {
  PendingEvent e{};
  e.targetOwner=targetOwner;
  if (!copy(e.path, sizeof(e.path), path) || !copy(e.name, sizeof(e.name), event) ||
      measureJson(data) >= sizeof(e.data))
    return;
  serializeJson(data, e.data, sizeof(e.data));
  if (xQueueSend(eventQueue, &e, 0) != pdTRUE)
    ++eventDrops;
}
static void drainEvents() {
  PendingEvent e;
  for (int i = 0; i < 32 && xQueueReceive(eventQueue, &e, 0) == pdTRUE; i++) {
    JsonDocument d;
    deserializeJson(d, e.data);
    if (!strcmp(e.path, "/dev/display") &&
        (!strcmp(e.name, "completed") || !strcmp(e.name, "failed"))) {
      uint32_t id = d["job_id"] | 0;
      if (jobs[id % 8].id == id) {
        auto &j = jobs[id % 8];
        j.complete = true;
        j.ok = d["success"] | false;
        j.total = d["total_ms"] | 0;
        copy(j.error, sizeof(j.error), d["error"] | "none");
      }
    }
    core.clock(uptime());
    core.emit(e.path, e.name, d.as<JsonObjectConst>(),e.targetOwner);
  }
}
static void event(const char *path, const char *name, const char *value = nullptr) {
  JsonDocument d;
  auto o = d.to<JsonObject>();
  if (value)
    o["value"] = value;
  emit(path, name, o);
}
static bool takeSPI() {
  return xSemaphoreTake(spiLease, 0) == pdTRUE;
}
static void giveSPI() {
  xSemaphoreGive(spiLease);
}
static void spiIdle() {
  SPI.end();spiStarted=false;
  digitalWrite(pins::SCLK, LOW);
  digitalWrite(pins::MOSI, LOW);
  digitalWrite(pins::SD_CS, LOW);
  pinMode(pins::SCLK, OUTPUT);
  pinMode(pins::MOSI, OUTPUT);
  pinMode(pins::SD_CS, OUTPUT);
  pinMode(pins::MISO, INPUT);
  gpio_set_pull_mode(gpio_num_t(pins::MISO), GPIO_FLOATING);
}
#include "../SdLinkService.h"
// Caller owns SPI lease. Shared-rail users only put a card in SPI mode here;
// filesystem tuning is deferred until storage is actually requested.
static const char *ensureRail() {
  if (!rail) {
    digitalWrite(pins::PERIPHERAL_ENABLE, LOW);
    rail = true;
    delay(10);
  }
  digitalWrite(pins::PANEL_CS, HIGH);
  digitalWrite(pins::SD_CS, HIGH);
  if(!spiStarted){SPI.begin(pins::SCLK, pins::MISO, pins::MOSI, -1);spiStarted=true;}
  if (!cardProbed) {
    cardProbed=true;
    SD.cardBegin(SdSpiConfig(pins::SD_CS,SHARED_SPI|USER_SPI_BEGIN,1000000,&boardSdSpi));
    // Optional media must not delay the built-in UI with a full speed sweep.
  }
  return nullptr;
}
static const char *ensureSdReady(){
  if(auto e=ensureRail())return e;
  if(!sdTuned||!sdConfiguration(policies).sameLink(sdLink.applied))sdMountConfigured();
  if(sdLink.outOfSpec&&!policies.get<bool>("/sys/bus","sd_allow_experimental_io",false))return "experimental_link_only";
  if(sdLink.ready&&!sdVolumeAttempted)sdMountVolume();
  return mounted?nullptr:"sd_mount_failed";
}
static const char *railOff() {
  if(vfs.hasLeasesUnder("/storage/user")||vfs.pinnedPath("/storage/user"))return "storage_in_use";
  if (audioRunning || audioSpooling || displayActive() || streaming)
    return "resource_busy";
  if (!takeSPI())
    return "spi_busy";
  if(!panel.safeToIdle()||!colorPanel.safeToIdle()){giveSPI();return "display_not_safe_to_idle";}
  if (cardProbed) {
    SD.end();
  }
  mounted=false;cardProbed=false;sdTuned=false;
  sdVolumeSnapshot(false);
  sdVolumeAttempted=false;
  sdCanWrite=true;sdSetWritable(true);
  sdLink.requestedHz=sdLink.effectiveHz=0;
  sdLink.ready=false;sdLink.highSpeedEnabled=false;sdLink.outOfSpec=false;
  sdLink.partition=-1;
  copy(sdLink.activeMode,sizeof(sdLink.activeMode),"none");
  copy(sdLink.error,sizeof(sdLink.error),"powered_off");
  copy(sdLink.volumeError,sizeof(sdLink.volumeError),"not_attempted");
  spiIdle();
  digitalWrite(pins::MIC_WS, LOW);
  digitalWrite(pins::MIC_BCLK, LOW);
  pinMode(pins::MIC_WS, OUTPUT);
  pinMode(pins::MIC_BCLK, OUTPUT);
  digitalWrite(pins::PERIPHERAL_ENABLE, HIGH);
  rail = false;
  giveSPI();
  return nullptr;
}
static uint32_t readBatteryAdcMv() {
  uint32_t total = 0;
  for (int i = 0; i < 8; i++)
    total += analogReadMilliVolts(pins::BATTERY);
  return total/8;
}
static bool imuRead(uint8_t reg,uint8_t*b,uint8_t n){
 if(!i2cLease||xSemaphoreTake(i2cLease,0)!=pdTRUE)return false;
 Wire.beginTransmission(imuAddress);Wire.write(reg);bool ok=Wire.endTransmission(false)==0;
 if(ok)ok=Wire.requestFrom(imuAddress,n)==n;if(ok)for(unsigned i=0;i<n;++i)b[i]=Wire.read();xSemaphoreGive(i2cLease);return ok;
}
static bool imuWrite(uint8_t reg,uint8_t value){
 if(!i2cLease||xSemaphoreTake(i2cLease,0)!=pdTRUE)return false;
 Wire.beginTransmission(imuAddress);Wire.write(reg);Wire.write(value);bool ok=Wire.endTransmission()==0;xSemaphoreGive(i2cLease);return ok;
}
static MotionConfig motionConfig;static OrientationInput orientationInput;static uint64_t imuSampleAt=0;static bool imuConfigOk=true;
static bool motionConfigure(bool on){
 if(!imuFound)return !on;
 // SC7A20H v0.7, sections 12.4/12.6/12.24-29. Click engine uses 200 Hz;
 // public timing is exact (100 + n*320 ms), never a rounded hidden register.
 bool tap=motionConfig.tap;
 bool ok=imuWrite(0x22,0)&&imuWrite(0x38,0)&&imuWrite(0x23,tap?0x90:0x80)&&imuWrite(0x20,tap?0x67:on?0x2f:0x37)&&imuWrite(0x21,on?1:0)&&imuWrite(0x24,on?8:0);
 if(on)ok=ok&&imuWrite(0x32,0x10)&&imuWrite(0x33,2)&&imuWrite(0x30,0x2a);
 if(tap)ok=ok&&imuWrite(0x3a,0x50|MotionConfig::thresholdIndex(motionConfig.tapThreshold1,false))&&imuWrite(0x3b,0x82|(MotionConfig::thresholdIndex(motionConfig.tapThreshold2,true)<<3))&&imuWrite(0x3c,0x04)&&imuWrite(0x3d,uint8_t(((motionConfig.tapWindowMs-100)/320)<<4)|motionConfig.tapCount)&&imuWrite(0x38,0x0f);
 uint8_t ignored;ok=ok&&imuRead(0x31,&ignored,1)&&imuRead(0x39,&ignored,1)&&imuWrite(0x22,(on?0x40:0)|(tap?0x80:0));return ok;
}
static void pollImu(){
 if(!imuFound||!imuConfigOk)return;uint64_t now=uptime();if(now-imuSampleAt<motionConfig.sampleMs)return;imuSampleAt=now;
 if(motionConfig.orientation){uint8_t b[6];if(imuRead(0xa8,b,6)){int xyz[3];for(int i=0;i<3;++i)xyz[i]=int16_t(b[i*2]|uint16_t(b[i*2+1])<<8);
  int direction=orientationInput.sample(xyz[0],xyz[1],xyz[2],now,motionConfig);if(direction>=0){JsonDocument d;d["rotation"]=direction;d["captured_at_ms"]=now;emit("/dev/imu","orientation",d.as<JsonObjectConst>());}}}
 if(digitalRead(pins::IMU_INT)){
  uint8_t source=0;if(motionConfig.tap&&imuRead(0x39,&source,1)&&(source&15)){JsonDocument d;d["count"]=source&15;d["captured_at_ms"]=now;emit("/dev/imu","tap",d.as<JsonObjectConst>());}
  if(wake.imu&&imuRead(0x31,&source,1)&&(source&0x40)){JsonDocument d;d["axes"]=source&63;d["captured_at_ms"]=now;emit("/dev/imu","motion",d.as<JsonObjectConst>());}
 }
}
static void hex(const uint8_t *p, size_t n, char *out) {
  static const char a[] = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[2 * i] = a[p[i] >> 4];
    out[2 * i + 1] = a[p[i] & 15];
  }
  out[2 * n] = 0;
}
static bool unhex(const char *s, uint8_t *out, size_t cap, size_t &n) {
  if (!s || strlen(s) % 2 || strlen(s) / 2 > cap)
    return false;
  n = strlen(s) / 2;
  auto nib = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
  };
  for (size_t i = 0; i < n; i++) {
    int a = nib(s[2 * i]), b = nib(s[2 * i + 1]);
    if (a < 0 || b < 0)
      return false;
    out[i] = (a << 4) | b;
  }
  return true;
}

static const char *common(const char *method, JsonObjectConst a, JsonObject out) {
  if (!strcmp(method, "list")) {
    unsigned cursor=a["cursor"]|0u,limit=a["limit"]|MAX_DEVICES;
    if((a.containsKey("cursor")&&!a["cursor"].is<uint32_t>())||(a.containsKey("limit")&&!a["limit"].is<uint32_t>())||!limit||limit>MAX_DEVICES)return "discovery_range";
    core.list(out["devices"].to<JsonArray>(),cursor,limit,a["details"]|true);out["next_cursor"]=cursor+out["devices"].size();out["more"]=cursor+out["devices"].size()<core.deviceCount();
    out["abi"] = "notes-device/1";
    out["board"] = boardpack::ID;
    out["boot_id"] = bootId;
    return nullptr;
  }
  if(!strcmp(method,"describe"))return core.describe(a["path"]|"",out);
  return "unsupported";
}
static const char *eventsHandler(const char *, JsonObjectConst a, JsonObject out) {
  core.events(a["after"] | 0, a["limit"] | 8, out);
  out["producer_drops"] = eventDrops.load();
  return nullptr;
}
static const char *rulesHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if (!strcmp(m, "read")) {
    core.listRules(out["rules"].to<JsonArray>());
    return nullptr;
  }
  if (!strcmp(m, "write"))
    return core.setRule(a, out);
  return core.removeRule(a["id"] | 0) ? nullptr : "rule_not_found";
}
static const char*conditionsHandler(const char*m,JsonObjectConst a,JsonObject out){
  if(!strcmp(m,"read")){conditions.list(out);return nullptr;}
  if(!strcmp(m,"write"))return conditions.set(a,out);
  if(!strcmp(m,"remove"))return conditions.remove(a["id"]|"");
  return "unsupported_method";
}
static const char *timerHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if (!strcmp(m, "read")) {
    core.listTimers(out["timers"].to<JsonArray>());
    out["survives_deep_sleep"] = false;
    return nullptr;
  }
  if (!strcmp(m, "write"))
    return core.timer(a, out);
  return core.removeTimer(a["id"] | 0) ? nullptr : "timer_not_found";
}
static const char *jobsHandler(const char *, JsonObjectConst a, JsonObject out) {
  uint32_t id = a["job_id"] | 0;
  if (!id || jobs[id % 8].id != id)
    return "job_unknown_or_evicted";
  auto &j = jobs[id % 8];
  out["job_id"] = id;
  out["status"] = j.complete ? (j.ok ? "completed" : "failed") : "queued_or_running";
  out["state"] = j.state;
  out["total_ms"] = j.total;
  out["error"] = j.error;
  return nullptr;
}
static const char *boardHandler(const char *, JsonObjectConst, JsonObject out) {
  out["board"] = boardpack::ID;
  out["boot_id"] = bootId;
  out["mcu"] = boardpack::MCU;out["package_fingerprint"]=boardpack::FINGERPRINT;
  out["clock_crystal_mhz"] = boardpack::CRYSTAL_MHZ;
  out["clock_load_pf"] = boardpack::CRYSTAL_LOAD_PF;
  auto mapping = out["pins"].to<JsonArray>();
  for (auto pin:boardpack::PINS) {
    auto p=mapping.add<JsonObject>();p["gpio"]=pin.gpio;p["package_pin"]=pin.pad;
    p["signal"]=pin.signal;p["lp_gpio"]=pin.gpio<=7;
  }
  out["validation"] = "compiled_not_tested_on_new_board";
  out["mac"] = WiFi.macAddress();
  out["heap_free"] = ESP.getFreeHeap();
  out["heap_largest"] = ESP.getMaxAllocHeap();
  out["reset_reason"] = int(esp_reset_reason());
  out["wake_cause"] = int(esp_sleep_get_wakeup_cause());
  return nullptr;
}
#include "../EspTimeService.h"
static const char *clockHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if (!strcmp(m, "write")) {
    ClockChange change;if(auto e=validateClockChange(a,change,{80,160}))return e;
    if(change.cpu&&(audioRunning||audioSpooling||displayActive()||streaming||networkActive()))return "resource_busy";
    uint32_t previous=getCpuFrequencyMhz();
    if(change.cpu&&!setCpuFrequencyMhz(change.cpuMhz))return "clock_failed";
    if(change.wall){timeval t{time_t(change.unixSeconds),0};
      if(settimeofday(&t,nullptr)){
        if(change.cpu&&!setCpuFrequencyMhz(previous))return "clock_rollback_failed";
        return "clock_failed";
      }
      timeWasSet=true;
    }
  }

  out["monotonic_ms"] = uptime();
  out["unix_s"] = int64_t(time(nullptr));
  out["wall_clock_valid"] = timeWasSet.load() || time(nullptr) > 1700000000;
  out["cpu_mhz"] = getCpuFrequencyMhz();
  out["rtc_slow_source"] = "compiled_internal_default";
  return nullptr;
}
static const char* policyHandler(const char*,JsonObjectConst,JsonObject);
static ButtonGestures buttonGestures({"A","B","C"},nullptr,[](void*,const char*n,JsonObjectConst o){emit("/dev/buttons",n,o);});
static bool persistButtons(const char*s,size_t n){return preferences.putString("buttons",s)==n;}
static const char*buttonHandler(const char*m,JsonObjectConst a,JsonObject o){
 if(!strcmp(m,"describe")){ButtonGestures::describe(o);return nullptr;}
 if(!inputMutex)return "input_unavailable";
 if(xSemaphoreTake(inputMutex,pdMS_TO_TICKS(20))!=pdTRUE)return "input_busy";
 if(!strcmp(m,"read")){buttonGestures.read(o);xSemaphoreGive(inputMutex);return nullptr;}
 auto prepared=buttonGestures;xSemaphoreGive(inputMutex);
 if(strcmp(m,"configure"))return "unsupported_method";
 if(auto e=prepared.configure(a,o,persistButtons))return e;
 // NVS commit happens outside the sampling lock. Commit only the configuration,
 // never the sample/gesture state that has advanced while Flash was busy.
 xSemaphoreTake(inputMutex,portMAX_DELAY);buttonGestures.commitConfiguration(prepared);buttonGestures.read(o);xSemaphoreGive(inputMutex);return nullptr;
}
static const char *imuHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!imuFound)return "imu_not_found";
 if(!strcmp(m,"describe")){imuDescription(out);out["tap_count_range"]="1..15";out["tap_window_ms"]="100 + 320*n, n=0..15";out["tap_sample_hz"]=200;out["tap_threshold1_mg_choices"]="64,128,256,384,512,640,768,960";out["tap_threshold2_mg_choices"]="128,256,512,768,1024,1280,1536,1792";out["tap_detection"]="SC7A20H hardware click engine";out["physical_validation"]="pending";out["configuration_scope"]="device";out["apply_time"]="immediate";out["sample_ms_range"]="20..2000";out["stable_ms_range"]="20..10000";out["rotation_offset_choices"]="0,90,180,270";out["dominance_range"]="0.05..0.8";return nullptr;}
 if(!strcmp(m,"configure")){
  for(auto f:a)if(!Policies::choice("settings,persist",f.key().c_str()))return "unknown_motion_config";
  if(!a["settings"].is<JsonObjectConst>()||(a.containsKey("persist")&&!a["persist"].is<bool>()))return "motion_config_type";
  auto previous=motionConfig,next=previous;if(auto e=MotionConfig::merge(a["settings"].as<JsonObjectConst>(),next))return e;
  motionConfig=next;if(!motionConfigure(wake.imu)){motionConfig=previous;imuConfigOk=motionConfigure(wake.imu);return imuConfigOk?"imu_config_failed":"imu_config_rollback_failed";}
  if(a["persist"]|false){JsonDocument d;motionConfig.describe(d.to<JsonObject>());String text;serializeJson(d,text);if(preferences.putString("motion",text)!=text.length()){motionConfig=previous;imuConfigOk=motionConfigure(wake.imu);return "persistence_failed";}}
  orientationInput.reset();imuConfigOk=true;
 }else if(!strcmp(m,"write")){for(auto f:a)if(strcmp(f.key().c_str(),"motion"))return "unknown_motion_setting";if(!a["motion"].is<bool>())return "motion_type";bool on=a["motion"];if(!motionConfigure(on)){imuConfigOk=motionConfigure(wake.imu);return "imu_io_error";}wake.imu=on;}
 else if(!strcmp(m,"clear")){uint8_t source;if(!imuRead(0x31,&source,1))return "imu_io_error";out["interrupt_source"]=source;return nullptr;}
 else if(strcmp(m,"read"))return "unsupported_method";
 if(!imuConfigOk)return "imu_configuration_fault";uint8_t b[6];if(!imuRead(0xa8,b,6))return "imu_io_error";
 auto xyz=out["raw"].to<JsonArray>();for(int i=0;i<3;++i)xyz.add(int16_t(b[2*i]|uint16_t(b[2*i+1])<<8));
 out["address"]=imuAddress;out["interrupt"]=bool(digitalRead(pins::IMU_INT));out["motion"]=wake.imu;out["i2c_hz"]=pins::I2C_HZ;out["rotation"]=orientationInput.current();motionConfig.describe(out["configured"].to<JsonObject>());return nullptr;
}
static const char *wakeHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if (!strcmp(m, "write")) {
    for(auto field:a)if(!Policies::choice("buttons_mask,timer_ms,imu,display_busy",field.key().c_str()))return "unknown_wake_setting";
    for(auto key:{"buttons_mask","timer_ms"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "wake_integer_required";
    for(auto key:{"imu","display_busy"})if(a.containsKey(key)&&!a[key].is<bool>())return "wake_boolean_required";
    unsigned keys = a["buttons_mask"] | wake.buttons;
    uint32_t timer = a["timer_ms"] | wake.timerMs;
    if (keys > 7 || timer > 604800000)
      return "wake_range";
    bool motion = a["imu"] | wake.imu, busy = a["display_busy"] | wake.busy;
    if (busy)return "display_busy_sleep_not_implemented";
    if (motion && !imuFound)
      return "imu_not_found";
    if (!keys && !timer && !motion && !busy)
      return "wake_source_required";
    wake = {uint8_t(keys), motion, busy, timer};
  }
  out["buttons_mask"] = wake.buttons;
  out["imu"] = wake.imu;
  out["display_busy"] = wake.busy;
  out["timer_ms"] = wake.timerMs;
  return nullptr;
}
#include "../WifiService.h"
static const char *usbHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if (!strcmp(m, "write"))
    usbEvents = a["events"] | false;
  out["connected"] = bool(Serial);
  out["events"] = usbEvents;
  out["function"] = "usb_serial_jtag_cdc";
  out["line_limit"] = MAX_WIRE;
  return nullptr;
}
#include "../StorageService.h"
#include "../PowerService.h"
#include "../AudioStorage.h"
#include "../NetworkService.h"
static void audioTask(void *) {
  i2s_chan_handle_t channel = nullptr;
  i2s_chan_config_t cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  cfg.dma_desc_num = 4;
  cfg.dma_frame_num = 160;
  esp_err_t err = i2s_new_channel(&cfg, nullptr, &channel);
  if (err == ESP_OK) {
    i2s_std_config_t s{};
    s.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(micRate);
    s.slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO);
    s.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    s.gpio_cfg.bclk = gpio_num_t(pins::MIC_BCLK);
    s.gpio_cfg.ws = gpio_num_t(pins::MIC_WS);
    s.gpio_cfg.dout = I2S_GPIO_UNUSED;
    s.gpio_cfg.din = gpio_num_t(pins::MIC_DATA);
    err = i2s_channel_init_std_mode(channel, &s);
  }
  if (err == ESP_OK) {
    gpio_set_pull_mode(gpio_num_t(pins::MIC_DATA), GPIO_PULLDOWN_ONLY);
    err = i2s_channel_enable(channel);
  }
  if (err == ESP_OK) {
    int32_t raw[320];
    size_t got;
    i2s_channel_read(channel, raw, sizeof(raw), &got, 30);
    {JsonDocument d;d["recording_id"]=recordingId;d["owner"]=micOwner;emit("/dev/mic","started",d.as<JsonObjectConst>());}
    uint64_t sampleAt=0;VoiceCodec codec;
    while (!audioStop && (!micLimit || (uptime() - micStarted) < micLimit)) {
      got = 0;
      err = i2s_channel_read(channel, raw, sizeof(raw), &got, 50);
      if (err != ESP_OK)
        break;
      // Each bounded event is <= 160 PCM16 samples (10 ms at 16 kHz).
      if(got<8)continue;
      AudioChunk chunk{};
      chunk.sequence = audioSequence++;chunk.sampleStart=sampleAt;
      chunk.samples = got / 8;
      if (chunk.samples > 160)
        chunk.samples = 160;
      chunk.bytes=11+(chunk.samples+1)/2;chunk.data[0]=0x42;
      for(int n=0;n<4;++n)chunk.data[n+1]=uint8_t(chunk.sequence>>(8*n));
      chunk.data[5]=uint8_t(codec.predictor);chunk.data[6]=uint8_t(uint16_t(codec.predictor)>>8);chunk.data[7]=codec.index;chunk.data[8]=0;chunk.data[9]=uint8_t(chunk.samples);chunk.data[10]=uint8_t(chunk.samples>>8);
      for (unsigned i = 0; i < chunk.samples; i++) {
        int32_t x = raw[i * 2] >> 14;
        if (x > 32767)
          x = 32767;
        if (x < -32768)
          x = -32768;
        uint8_t nibble=codec.encode(int16_t(x));if(i&1)chunk.data[11+i/2]|=nibble<<4;else chunk.data[11+i/2]=nibble;
      }
      sampleAt+=chunk.samples;producedSamples=sampleAt;
      if(audioToNetwork&&xQueueSend(audioQueue,&chunk,0)!=pdTRUE){audioDrops+=chunk.samples;if(audioStopOnOverflow)audioStop=true;}
      if(audioToDisk&&xQueueSend(spoolQueue,&chunk,0)!=pdTRUE){audioStorageDrops+=chunk.samples;if(audioStopOnOverflow)audioStop=true;}
    }
  }
  if (channel) {
    i2s_channel_disable(channel);
    i2s_del_channel(channel);
  }
  digitalWrite(pins::MIC_BCLK, LOW);
  digitalWrite(pins::MIC_WS, LOW);
  pinMode(pins::MIC_BCLK, OUTPUT);
  pinMode(pins::MIC_WS, OUTPUT);
  audioCaptureFailed=err!=ESP_OK;
  {JsonDocument d;d["recording_id"]=recordingId;d["owner"]=micOwner;d["error"]=esp_err_to_name(err);emit("/dev/mic",err==ESP_OK?"stopped":"failed",d.as<JsonObjectConst>());}
  audioRunning = false;
  vTaskDelete(nullptr);
}
static const char *micHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if(strcmp(m,"read")&&strcmp(m,"start")&&core.callerOwner()&&micOwner!=core.callerOwner())return "mic_owned_by_other";
  if (!strcmp(m, "read_chunk")) {
    if(a.containsKey("limit")&&!a["limit"].is<uint32_t>())return "audio_read_limit";
    uint32_t limit=a["limit"]|4u;if(!limit||limit>16)return "audio_read_limit";
    if(a.containsKey("recording_id")&&strcmp(a["recording_id"]|"",recordingId))return "recording_replaced";
    audioReadableNotified=false;
    auto arr = out["chunks"].to<JsonArray>();
    AudioChunk c;
    for (uint32_t i = 0; i < limit && xQueueReceive(audioQueue, &c, 0) == pdTRUE; i++) {
      char b64[125];
      size_t n = 0;
      if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(b64), sizeof(b64), &n, c.data,
                                c.bytes) != 0 ||
          n >= sizeof(b64))
        return "audio_encoding_failed";
      b64[n] = 0;
      auto x = arr.add<JsonObject>();
      x["sequence"] = c.sequence;
      x["samples"] = c.samples;sizeText(x,"sample_start",c.sampleStart);
      x["adpcm_b64"] = b64;
    }
    out["encoding"]="ima-adpcm";out["sample_rate"]=micRate;out["recording_id"]=recordingId;out["dropped_samples"] = audioDrops.load();
    out["recording"] = bool(audioRunning);out["produced_samples"]=producedSamples.load();out["buffered_chunks"]=uxQueueMessagesWaiting(audioQueue);out["capture_failed"]=audioCaptureFailed.load();
    return nullptr;
  }
  if (!strcmp(m, "start")) {
    if(auto error=validateMicArguments(a))return error;
    if (audioRunning || audioSpooling || shutdownRequested || pendingPower[0])
      return "mic_busy";
    JsonDocument config;policies.resolve("/dev/mic",a["scope"]|appScope,config.to<JsonObject>());
    unsigned r = a["sample_rate"] | (config["sample_rate_hz"]|16000u);
    unsigned l = a.containsKey("max_ms") ? (a["max_ms"].isNull()?0:a["max_ms"].as<uint32_t>()) : (config["max_duration_ms"].isNull()?0:config["max_duration_ms"].as<uint32_t>());
    if ((r != 16000 && r != 48000) || (a.containsKey("max_ms")&&!a["max_ms"].isNull()&&!a["max_ms"].is<uint32_t>()))
      return "mic_range";
    const char*storage=a["storage"]|(config["storage"]|"off");
    bool disk=strcmp(storage,"off"),network=a["network_queue"]|(config["network_queue"]|true);
    if(!disk&&!network)return "audio_sink_required";
    if (!rail || !cardProbed) {
      if (!takeSPI())
        return "spi_busy";
      const char *e = ensureRail();
      giveSPI();
      if (e)
        return e;
    }
    if (ESP.getFreeHeap() < 45000 || ESP.getMaxAllocHeap() < 16000)
      return "audio_memory_low";
    const char*storageFallback=nullptr;
    audioToDisk=disk;audioToNetwork=network;
    if(audioToDisk&&!mounted){
      if(!takeSPI())return "spi_busy";
      const char *e=ensureSdReady();giveSPI();
      if(e){if(audioToNetwork&&!strcmp(config["on_storage_error"]|"continue","continue")){audioToDisk=false;storageFallback="sd_not_available";}else return "storage_not_available";}
    }
    if(audioToDisk&&!sdCanWrite){if(audioToNetwork&&!strcmp(config["on_storage_error"]|"continue","continue")){audioToDisk=false;storageFallback="storage_read_only";}else return "storage_read_only";}
    audioStopOnOverflow=!strcmp(config["on_overflow"]|"drop","stop");
    audioStopOnStorageError=!strcmp(config["on_storage_error"]|"continue","stop");
    copy(recordingRetention,sizeof(recordingRetention),storage);
    snprintf(recordingId,sizeof(recordingId),"%s-%lu",bootId,(unsigned long)nextRecording++);
    snprintf(recordingFile,sizeof(recordingFile),"/recordings/%s.adpcm",recordingId);
    audioStorageError=storageFallback?storageFallback:"none";audioStorageDrops=0;audioCaptureFailed=false;
    audioReadableNotified=false;xQueueReset(audioQueue);xQueueReset(spoolQueue);
    audioDrops = 0;
    micOwner=core.callerOwner();
    micRate = r;
    micLimit = l;
    micStarted = uptime();
    audioStop = false;
    producedSamples=0;audioRunning = true;
    audioSequence = 0;
    if(audioToDisk){if(auto error=beginAudioStorage(config.as<JsonObjectConst>())){audioStorageError=error;if(audioToNetwork&&!audioStopOnStorageError)audioToDisk=false;else{audioRunning=false;return error;}}}
    if (xTaskCreate(audioTask, "notes-mic", 6144, nullptr, 3, nullptr) != pdPASS) {
      audioCaptureFailed=true;audioRunning = false;
      return "task_allocation_failed";
    }
    out["status"] = "starting";out["recording_id"]=recordingId;out["storage_active"]=audioToDisk;out["storage_error"]=audioStorageError.load();out["encoding"]="ima-adpcm";
    return nullptr;
  }
  if (!strcmp(m, "stop"))
    audioStop = true;
  if(recordings)recordings->read(out["retention"].to<JsonObject>());
  out["recording_id"]=recordingId;out["file"]=audioToDisk?String("/storage/user")+recordingFile:String("");out["storage_pending"]=bool(audioSpooling);out["storage_error"]=audioStorageError.load();out["capture_failed"]=audioCaptureFailed.load();out["storage_dropped_samples"]=audioStorageDrops.load();
  out["recording"] = bool(audioRunning);
  out["sample_rate"] = micRate;
  if(micLimit)out["max_ms"] = micLimit;else out["max_ms"]=nullptr;
  out["shared_chipen"] = rail;
  out["storage_active"]=audioToDisk;out["storage_requested"]=recordingRetention;out["encoding"] = "ima-adpcm";out["queue_duration_ms"]=uint64_t(144)*160*1000/micRate;
  out["dropped_samples"] = audioDrops.load();
  out["buffered_chunks"] = uxQueueMessagesWaiting(audioQueue);
  return nullptr;
}
static void finishHash(char *out) {
  uint8_t hash[32];
  mbedtls_sha256_finish(&digest, hash);
  mbedtls_sha256_free(&digest);
  hex(hash, 32, out);
}
#include "../BleService.h"
#include "../WebSocketService.h"
#include "../MeshService.h"
static bool applicationNetworkActive(){return tcpService.active()||udpService.active()||httpApps.active()||wsClient;}
static const char*tcpHandler(const char*m,JsonObjectConst a,JsonObject o){return tcpService.invoke(m,a,o);}
static const char*udpHandler(const char*m,JsonObjectConst a,JsonObject o){return udpService.invoke(m,a,o);}
static const char*httpAppsHandler(const char*m,JsonObjectConst a,JsonObject o){return httpApps.invoke(m,a,o);}
static const char*remoteHandler(const char*m,JsonObjectConst a,JsonObject o){
 if(!a["expected_boot_id"].isNull()&&strcmp(a["expected_boot_id"]|"",bootId))return "boot_changed";
 return remoteRequests.invoke(m,a,o,uptime(),bootId);
}
#include "../ScriptService.h"
#include "../WorkerService.h"
#include "../UiService.h"
#include "E6UiSurface.inc"
#include "E6Display.inc"
static const char *powerHandler(const char *m, JsonObjectConst a, JsonObject out) {
  if(workers.active()&&Policies::choice("sleep,shutdown,reboot",m))return "workers_active";
  if (!strcmp(m, "read")) {
    out["state"] = pendingPower[0] ? pendingPower : operatingMode;
    out["cpu_mhz"]=getCpuFrequencyMhz();
    wifi_ps_type_t ps;if(esp_wifi_get_ps(&ps)==ESP_OK)out["wifi_power_save"]=ps==WIFI_PS_NONE?"off":ps==WIFI_PS_MIN_MODEM?"minimum":"maximum";
    out["peripherals_powered"] = rail;
    out["shutdown_pin"] = digitalRead(pins::SHUTDOWN);
    out["real_power_cut_confirmed"] = false;
    return nullptr;
  }
  if (!strcmp(m, "peripherals_off"))
    return railOff();
  if (audioRunning || audioSpooling || displayActive() || streaming || networkActive() || workers.active() || pendingPower[0] || shutdownRequested)
    return "resource_busy";
  if (!strcmp(m, "write")) {
    const char *mode = a["mode"] | "";
    if (strcmp(mode, "normal") && strcmp(mode, "low_power"))
      return "normal_or_low_power_required";
    const bool low = !strcmp(mode, "low_power");
    JsonDocument profile;policies.resolve("/sys/power","system",profile.to<JsonObject>());
    uint32_t cpu=profile[low?"low_power_cpu_mhz":"normal_cpu_mhz"];
    const char*save=profile[low?"low_power_wifi_save":"normal_wifi_save"];
    wifi_ps_type_t next=!strcmp(save,"off")?WIFI_PS_NONE:!strcmp(save,"minimum")?WIFI_PS_MIN_MODEM:WIFI_PS_MAX_MODEM;
    wifi_ps_type_t previous=WIFI_PS_NONE;bool wifiOn=WiFi.getMode()!=WIFI_OFF;
    if(wifiOn&&esp_wifi_get_ps(&previous)!=ESP_OK)return "wifi_ps_read_failed";
    if(wifiOn&&esp_wifi_set_ps(next)!=ESP_OK)return "wifi_ps_failed";
    if(!setCpuFrequencyMhz(cpu)){
      if(wifiOn&&esp_wifi_set_ps(previous)!=ESP_OK)return "power_rollback_failed";
      return "clock_failed";
    }
    operatingMode = low ? "low_power" : "normal";
    out["state"] = operatingMode;
    return nullptr;
  }
  if(nfsService.active())return "nfs_service_active";
    if(applicationNetworkActive())return "application_network_active";
  if(!strcmp(m,"reboot")){if(bleOperation||zbStarting)return "radio_operation_active";copy(pendingPower,sizeof(pendingPower),"reboot");powerAt=millis()+100;out["status"]="accepted";return nullptr;}
  if(strcmp(m,"shutdown")&&(bleRunning||meshOwner!=MeshOwner::None))return "radio_service_active";
  if (!strcmp(m, "shutdown")) {
    auto payload = new (std::nothrow) nm::Payload;
    if (!payload)
      return "memory_low";
    const char *white = "{\"format\":\"scene-v1\",\"background\":1,\"commands\":[]}";
    payload->size = strlen(white);
    memcpy(payload->bytes, white, payload->size);
    nm::Scene scratch;
    nm::validate(*payload, scratch);
    const char *e = queueDisplay(payload, E6Profile::StockUI, true, out);
    if (!e)
      shutdownRequested = true;
    return e;
  }
  const char *mode = a["mode"] | "";
  if (strcmp(mode, "light") && strcmp(mode, "deep"))
    return "sleep_mode_required";
  if (!wake.buttons && !wake.timerMs && !wake.imu && !wake.busy)
    return "wake_source_required";
  if (core.nextTimer())
    return "cancel_local_timers_before_sleep";
  for (int i = 0; i < 3; i++)
    if (wake.buttons & (1 << i) && !digitalRead(pins::buttons[i]))
      return "wake_already_asserted";
  if (wake.imu) {
    if (!motionConfigure(true))
      return "imu_io_error";
    if (digitalRead(pins::IMU_INT))
      return "wake_already_asserted";
  }
  if (wake.busy && digitalRead(pins::BUSY))
    return "display_busy_wake_already_asserted";
  // BUSY wake only has meaning during a powered waveform. This BSP currently
  // forbids sleeping during that operation, so don't advertise an unusable path.
  if (wake.busy)
    return "display_busy_sleep_not_implemented";
  copy(pendingPower, sizeof(pendingPower), mode);
  powerAt = millis() + 100;
  out["status"] = "accepted";
  out["meaning"] = "sleep_requested_not_yet_committed";
  return nullptr;
}
static void response(JsonObjectConst input,JsonObject out){invokeEnvelope(core,input,out,bootId,uptime());}
static void sendHTTP() {
  if (http.arg("plain").length() > MAX_WIRE) {
    http.send(413, "application/json", "{\"ok\":false,\"error\":\"wire_limit\"}");
    return;
  }
  JsonDocument d, r;
  if (deserializeJson(d, http.arg("plain"), DeserializationOption::NestingLimit(8)) ||
      !d.is<JsonObject>()) {
    http.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid_json\"}");
    return;
  }
  response(d.as<JsonObjectConst>(), r.to<JsonObject>());
  String body;
  serializeJson(r, body);
  http.send(r["ok"].as<bool>() ? 200 : 409, "application/json", body);
}
static void commitPower() {
  if (!pendingPower[0] || int32_t(millis() - powerAt) < 0)
    return;
  if (audioRunning || audioSpooling || displayActive() || streaming || networkActive() || workers.active()) {
    pendingPower[0] = 0;shutdownRequested=false;
    event("/sys/power", "aborted", "activity_started");
    return;
  }
  if(!strcmp(pendingPower,"reboot")){Serial.flush();ESP.restart();return;}
  const bool shutdown = !strcmp(pendingPower, "shutdown"), deep = !strcmp(pendingPower, "deep");
  const char *e = railOff();
  if (e) {
    pendingPower[0] = 0;shutdownRequested=false;
    event("/sys/power", "aborted", e);
    return;
  }
  if (shutdown) {
    event("/sys/power", "cut_requested");
    Serial.flush();
    digitalWrite(pins::SHUTDOWN, HIGH);
    delay(1000);
    // If external 5V holds EN high, the MCU may remain powered. Record that fact.
    pendingPower[0] = 0;
    shutdownRequested = false;
    digitalWrite(pins::SHUTDOWN, LOW);
    event("/sys/power", "power_cut_not_achieved");
    return;
  }
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  uint64_t mask = 0;
  for (int i = 0; i < 3; i++)
    if (wake.buttons & (1 << i))
      mask |= 1ULL << pins::buttons[i];
  esp_err_t err = ESP_OK;
  if (mask)
    err = esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);
  if (err == ESP_OK && wake.imu)
    err = esp_sleep_enable_ext1_wakeup_io(1ULL << pins::IMU_INT, ESP_EXT1_WAKEUP_ANY_HIGH);
  // A maintenance wake occurs before 24h; deep sleep loses frame bytes, so
  // waking unattended can clear white but cannot reconstruct unknown content.
  uint64_t age = esp_clk_rtc_time() - lastRefreshRTC;
  uint32_t remaining = age >= 86220000000ULL ? 1 : uint32_t((86220000000ULL - age) / 1000);
  uint32_t timer = wake.timerMs ? min(wake.timerMs, remaining) : remaining;
  if (err == ESP_OK)
    err = esp_sleep_enable_timer_wakeup(uint64_t(timer) * 1000);
  if (err != ESP_OK) {
    pendingPower[0] = 0;shutdownRequested=false;
    event("/sys/power", "aborted", esp_err_to_name(err));
    return;
  }
  if (imuFound && !wake.imu) {
    if (!imuWrite(0x22, 0) || !imuWrite(0x20, 0)) {
      pendingPower[0] = 0;
      event("/sys/power", "aborted", "imu_powerdown_failed");
      return;
    }
  }
  event("/sys/power", "sleep_committed", deep ? "deep" : "light");
  Serial.flush();
  wifiManager.suspend();
  for (int p : {pins::PERIPHERAL_ENABLE, pins::SHUTDOWN, pins::PANEL_CS, pins::RESET, pins::DC,
                pins::SCLK, pins::MOSI, pins::SD_CS})
    gpio_hold_en(gpio_num_t(p));
  if (deep) {
    esp_deep_sleep_start();
  } else {
    err = esp_light_sleep_start();
    for (int p : {pins::PERIPHERAL_ENABLE, pins::SHUTDOWN, pins::PANEL_CS, pins::RESET, pins::DC,
                  pins::SCLK, pins::MOSI, pins::SD_CS})
      gpio_hold_dis(gpio_num_t(p));
    for (int p : {pins::KEY_A, pins::KEY_B, pins::KEY_C, pins::IMU_INT}) {
      rtc_gpio_hold_dis(gpio_num_t(p));
      rtc_gpio_deinit(gpio_num_t(p));
    }
    pendingPower[0] = 0;
    motionConfigure(wake.imu);
    wifiManager.resume();
    event("/sys/power", "woke", esp_err_to_name(err));
  }
}
static void inputTask(void *) {
 while(true){if(inputMutex&&xSemaphoreTake(inputMutex,0)==pdTRUE){for(size_t i=0;i<3;++i)buttonGestures.sample(i,!digitalRead(pins::buttons[i]),millis(),presentedJob.load());xSemaphoreGive(inputMutex);}vTaskDelay(pdMS_TO_TICKS(5));}
}
static const char*invocationGate(const char*,const char*method){
 if(!pendingPower[0]&&!shutdownRequested)return nullptr;
 return Policies::choice("read,list,describe,drivers,speeds,peek,stat,mounts,scan_results",method)?nullptr:"power_transition";
}
void begin() {
  Serial.setRxBufferSize(4096);Serial.begin(115200);
  preferences.begin("notes-os",false);
  notes_radio_select_at_boot(preferences.getUChar("mesh_boot",1));
  psramFiles.setPresent(ESP.getPsramSize()!=0);
  core.onOwnerRelease(&micOwner,[](void*,uint32_t owner){
    if(micOwner==owner){audioStop=true;micOwner=0;}
    if(wsClient&&wsOwner==owner)stopWebSocket();
    if(networkJob&&networkJob->owner==owner){networkJob->orphan=true;networkJob->cancel=true;}
  });
  resourceStartupError=configureEspMemory(core.resources());
  core.setInvocationGate(invocationGate);
  core.setEventGate(inputGate);core.setEventObserver(scriptEvent);
  vfs.add(ramFiles);if(psramFiles.available())vfs.add(psramFiles);vfs.add(sdFiles);vfs.add(systemFiles);
  policies.setValidator(validatePlatformPolicy);
  const char*factory="{}";for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/defaults.json"))factory=asset.data;
  String restored=preferences.getString("policy", factory);policyRestoreOk=policies.restore(restored.c_str());
  panelSpiHz=policies.get<uint32_t>("/sys/bus","display_spi_hz",16000000);
  inputMutex=xSemaphoreCreateMutex();i2cLease=xSemaphoreCreateMutex();
  buttonGestures.restore(preferences.getString("buttons","{}").c_str());
  ramFiles.budget(policies.get<uint32_t>("/sys/storage","ram_quota_bytes",32768));
  psramFiles.budget(policies.get<uint32_t>("/sys/storage","psram_quota_bytes",262144));
  Serial.setDebugOutput(false); // GPIO16 is SD_CS: disable application UART log routing.
  for (int p : {pins::PERIPHERAL_ENABLE, pins::SHUTDOWN, pins::PANEL_CS, pins::RESET, pins::DC,
                pins::SCLK, pins::MOSI, pins::SD_CS})
    gpio_hold_dis(gpio_num_t(p));
  snprintf(bootId, sizeof(bootId), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  spiLease = xSemaphoreCreateBinary();
  giveSPI();
  eventQueue = xQueueCreate(32, sizeof(PendingEvent));
  audioQueue = xQueueCreate(144, sizeof(AudioChunk));
  spoolQueue = xQueueCreate(48,sizeof(AudioChunk));

  if (!spiLease || !eventQueue || !audioQueue || !spoolQueue) {
    Serial.println("{\"error\":\"allocation_failed\"}");
    while (true)
      delay(1000);
  }
  digitalWrite(pins::SHUTDOWN, LOW);
  pinMode(pins::SHUTDOWN, OUTPUT);
  digitalWrite(pins::PERIPHERAL_ENABLE, HIGH);
  pinMode(pins::PERIPHERAL_ENABLE, OUTPUT);
  digitalWrite(pins::RESET, HIGH);
  pinMode(pins::RESET, OUTPUT_OPEN_DRAIN);
  digitalWrite(pins::DC, HIGH);
  pinMode(pins::DC, OUTPUT);
  digitalWrite(pins::PANEL_CS, HIGH);
  pinMode(pins::PANEL_CS, OUTPUT);
  for (int p : pins::buttons) {
    rtc_gpio_deinit(gpio_num_t(p));
    pinMode(p, INPUT);
  }
  pinMode(pins::BUSY, INPUT);
  rtc_gpio_deinit(gpio_num_t(pins::IMU_INT));
  pinMode(pins::IMU_INT, INPUT);
  pinMode(pins::BATTERY, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(pins::BATTERY, ADC_11db);

  pinMode(pins::RESET, OUTPUT_OPEN_DRAIN);
  SPI.end();
  spiIdle();
  Wire.begin(pins::SDA, pins::SCL);
  Wire.setClock(pins::I2C_HZ);
  Wire.setTimeOut(10);
  uint8_t id;
  for (uint8_t addr : {0x19, 0x18}) {
    imuAddress = addr;
    if (imuRead(0x0f, &id, 1) && id == 0x11) {
      imuFound = true;
      break;
    }
  }
  if(imuFound){JsonDocument d;if(!deserializeJson(d,preferences.getString("motion","{}"))){auto next=motionConfig;if(!MotionConfig::merge(d.as<JsonObjectConst>(),next))motionConfig=next;}imuConfigOk=motionConfigure(wake.imu);}
  Device devices[] = {
      {"/sys/connections","read,configure","application service endpoints",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{return connections.invoke(m,a,o,[](const char*s,size_t n){return preferences.putString("connections",s)==n;});}},
      {"/sys/resources","read,configure","shared memory reservations and native scheduling",[](const char*m,JsonObjectConst a,JsonObject o)->const char*{if(!strcmp(m,"configure")){if(core.callerOwner())return "system_policy_required";if(auto e=core.resources().configure(a))return e;}core.resources().read(o);return nullptr;}},
      {"/sys/workers","read,submit,cancel,release","isolated background Lua compute, bounded memory and core affinity",workerHandler,workerDescription},
      {"/sys/runtime","read,load,restart,stop,configure","runtime-loaded Lua applications with memory and execution budgets",scriptHandler,runtimeDescription},
      {"/services/nfs","read,start,stop,memory","on-demand NFSv3 TCP storage export",nfsHandler,nfsDescription},
      {"/net/tcp","read,listen,connect,write,close","bounded nonblocking TCP sockets",tcpHandler,tcpDescription},
      {"/net/udp","read,open,send,receive,close","IPv4 and IPv6 datagrams",udpHandler,udpDescription},
      {"/net/ws","read,connect,send,receive,close","WS/WSS client messages",wsHandler,wsDescription},
      {"/net/http-server","read,route,remove,respond,release,stop","application HTTP routes and deferred responses",httpAppsHandler,httpServerDescription},
      {"/services/remote","read,submit,peek,complete,release","transport-independent remote service/agent requests",remoteHandler,remoteDescription},
      {"/dev/ble","read,configure,start,restart,write,stop,connect,gatt_read,gatt_write,subscribe,scan,scan_results,scan_stop","runtime GATT characteristics and HID report map",bleHandler,bleDescription},
      {"/sys/radio","read,write","wireless overview; independent Wi-Fi/BLE controls and boot-selected IEEE 802.15.4",radioHandler,radioDescription},
      {"/net/thread","read,start,stop","Thread dataset and IPv6 network transport",threadHandler,threadDescription},
      {"/net/zigbee","read,start,pause,resume,permit_join,send","Zigbee network and raw APS datagrams",zigbeeHandler,zigbeeDescription},
      {"/net/http","read,request,receive,cancel,release","bounded asynchronous HTTP operations; HTTPS with explicit CA",networkHandler,httpDescription},
      {"/sys/ui","read,capabilities,create,open,update,activate,close,refresh,back,app_load,fonts,font_register,font_unregister","LVGL UI from VFS, configurable input bindings and SD fonts",uiHandler},
      {"/sys/cache","resolve,prepare","resolve a cache path without I/O; explicitly prepare its directory",cacheHandler},
      {"/fs", "mounts,stat,list,read,write,remove,mkdir,rename", "virtual file providers; byte offsets as decimal strings",fsHandler},
      {"/sys/storage","read,list","providers, volumes, quotas and physical capacity",storageHandler},
      {"/sys/bus","read,speeds","physical bus clocks and attainable SD clock ladder; settings under /sys/config",busHandler,busDescription},
      {"/sys/config","read,describe,validate,apply","revisioned policy settings; hardware constraints remain read-only",policyHandler},
      {"/sys/stats","read","timestamped memory, CPU and storage observations",statsHandler,statsDescription},
      {"/net/default","read,describe,write,configure,profiles,profile_set,profile_remove,connect,disconnect,resume_auto,off,scan_start,scan_results,scan_clear","default network adapter",wifiHandler,wifiDescription},
      {"/sys/capabilities", "list,describe", "discover statically bound drivers", common},
      {"/sys/jobs", "read", "bounded last eight display job receipts", jobsHandler},
      {"/sys/board", "read", "fixed GPIO/package map",
       boardHandler},
      {"/sys/events", "read", "bounded sequence cursor; gaps are observable", eventsHandler},
      {"/sys/rules", "read,write,remove", "volatile event to method rules with cooldown",
       rulesHandler},
      {"/sys/timers", "read,write,remove", "monotonic timers emit fired events", timerHandler},
      {"/sys/power", "read,write,sleep,shutdown,peripherals_off,reboot",
       "white refresh precedes true shutdown; SD rail shared with CHIPEN", powerHandler,powerDescription},
      {"/services/time","read,describe,configure,sync","asynchronous NTP clock synchronization",timeHandler},
      {"/sys/clock", "read,write", "CPU clock and wall time", clockHandler,clockDescription},
      {"/sys/wake", "read,write", "configured button inputs, IMU interrupt and timer", wakeHandler},
      {"/dev/battery", "read,write", "board power measurements", batteryHandler,batteryDescription},
      {"/sys/conditions", "read,write,remove", "shared property conditions and transition events", conditionsHandler,conditionDescription},
      {"/dev/buttons", "read,describe,configure", "configurable button gestures", buttonHandler,buttonsDescription},
      {"/dev/imu", "read,write,configure,describe,clear", "SC7A20H raw XYZ and latched motion interrupt", imuHandler,imuDescription},
      {"/dev/wifi", "read,describe,write,configure,profiles,profile_set,profile_remove,connect,disconnect,resume_auto,off,scan_start,scan_results,scan_clear", "Wi-Fi power save/TX limit and network connection",
       wifiHandler,wifiDescription},
      {"/dev/usb", "read,write", "USB Serial/JTAG CDC control", usbHandler,usbDescription},
      {"/dev/sd", "read,probe,mount,off",
       "content-blind card link handshake, separate filesystem mount and per-card SPI clock", sdHandler,sdDescription},
      {"/dev/mic", "read,start,stop,read_chunk", "compressed audio queue; high performance core only",
       micHandler,micDescription},

      {"/dev/display", "read,drivers,present,scene,clear,begin,chunk,commit,abort,recover",
       "discovered display drivers; streaming; non-preemptible physical presentation", displayHandler,displayDescription},
  };
  for (auto d : devices) {
    if(!strcmp(d.path,"/dev/imu")&&!imuFound)continue;
    if(!strcmp(d.path,"/net/thread")&&notes_radio_boot_mode()!=1)continue;
    if(!strcmp(d.path,"/net/zigbee")&&notes_radio_boot_mode()!=2)continue;
    core.add(d);
  }
  Network.begin();
  uint8_t mac[6]{};esp_read_mac(mac,ESP_MAC_WIFI_STA);
  snprintf(apSsid,sizeof apSsid,"Agentic-Notes-%02X%02X%02X",mac[3],mac[4],mac[5]);
  restoreWifi();wifiManager.poll(uptime());
  http.on("/",HTTP_GET,[]{JsonDocument r;if(httpApps.handle("/","GET","",uptime(),r.to<JsonObject>())){http.send(r["status"]|200,r["content_type"]|"text/plain",r["body"]|"");return;}for(auto a:boardpack::ASSETS)if(!strcmp(a.path,"/setup.html")){http.send(200,"text/html; charset=utf-8",a.data);return;}http.send(404,"text/plain","UI package missing");});
  http.onNotFound([]{
    JsonDocument r;const char*method=http.method()==HTTP_GET?"GET":http.method()==HTTP_POST?"POST":http.method()==HTTP_PUT?"PUT":http.method()==HTTP_DELETE?"DELETE":"OTHER";
    if(httpApps.handle(http.uri().c_str(),method,http.arg("plain").c_str(),uptime(),r.to<JsonObject>())){http.send(r["status"]|200,r["content_type"]|"text/plain",r["body"]|"");return;}
    http.send(404,"application/json","{\"error\":\"not_found\"}");
  });
  http.on("/os/invoke", HTTP_POST, sendHTTP);
  http.on("/os/devices", HTTP_GET, [] {
    JsonDocument d;
    core.list(d["devices"].to<JsonArray>());
    d["abi"] = "notes-device/1";
    d["boot_id"] = bootId;
    String s;
    serializeJson(d, s);
    http.send(200, "application/json", s);
  });
  http.begin();
  if (xTaskCreate(inputTask, "notes-input", 3072, nullptr, 3, nullptr) != pdPASS)
    event("/dev/buttons", "failed", "input_task_allocation_failed");
  connections.restore(preferences.getString("connections","{}").c_str());
  timeService.restore(preferences.getString("time_sync","{}").c_str());
  restoreBleConfiguration();restoreRuntime();
  core.clock(uptime());
  event("/sys/board", "boot", bootId);
  frameValid = false;
  if (!lastRefreshRTC)
    lastRefreshRTC = esp_clk_rtc_time();
}
void poll() {
  core.clock(uptime());
  if(!pendingPower[0]&&!shutdownRequested)scriptHost.tick(uptime());
  if(!pendingPower[0]&&!shutdownRequested){boardWifi.service();wifiManager.poll(uptime());}
  if(uxQueueMessagesWaiting(audioQueue)&&!audioReadableNotified){audioReadableNotified=true;JsonDocument d;d["recording_id"]=recordingId;d["buffered_chunks"]=uxQueueMessagesWaiting(audioQueue);emit("/dev/mic","readable",d.as<JsonObjectConst>());}
  workers.poll();timeService.poll(uptime(),WiFi.status()==WL_CONNECTED);
  pollAudioStorage();
  pollPowerSense();pollImu();
  pollDisplay();
  sampleStats();
  pollNetwork();tcpService.poll();udpService.poll();httpApps.tick(uptime());remoteRequests.tick(uptime());pollBle();pollMesh();if(wsClient)wsClient->loop();
  if(nfsService.poll(millis()))event("/services/nfs","handles_invalidated","local_storage_changed");
  drainEvents();
  dns.processNextRequest();
  http.handleClient();
  static char line[MAX_WIRE + 1];
  static size_t used = 0;
  static bool discard = false;
  for (int i = 0; i < 256 && Serial.available(); i++) {
    char c = Serial.read();
    if (c == '\n') {
      if (!discard && used) {
        line[used] = 0;
        JsonDocument d, r;
        if (deserializeJson(d, line, DeserializationOption::NestingLimit(8)) ||
            !d.is<JsonObject>()) {
          r["ok"] = false;
          r["error"] = "invalid_json";
        } else
          response(d.as<JsonObjectConst>(), r.to<JsonObject>());
        serializeJson(r, Serial);
        Serial.println();
      }
      used = 0;
      discard = false;
    } else if (c != '\r') {
      if (used < MAX_WIRE && !discard)
        line[used++] = c;
      else
        discard = true;
    }
  }
  static int wifiStatus = -1;
  int currentWiFi = int(WiFi.status());
  if (currentWiFi != wifiStatus) {
    wifiStatus = currentWiFi;
    JsonDocument d;
    auto o = d.to<JsonObject>();
    o["connected"] = currentWiFi == WL_CONNECTED;
    o["ssid"] = WiFi.SSID();
    o["ip"] = WiFi.localIP().toString();
    emit("/dev/wifi", "changed", o);
  }
  static bool serialSeen = false;
  bool serialNow = bool(Serial);
  if (serialNow != serialSeen) {
    serialSeen = serialNow;
    JsonDocument d;
    auto o = d.to<JsonObject>();
    o["connected"] = serialNow;
    emit("/dev/usb", "changed", o);
  }
  bool irq = digitalRead(pins::IMU_INT), busy = !digitalRead(pins::BUSY);
  if (imuFound && irq && !lastImu)
    event("/dev/imu", "interrupt");
  lastImu = irq;
  if (busy != lastBusy) {
    event("/dev/display", busy ? "busy" : "ready");
    lastBusy = busy;
  }
  // A confirmed low condition remains actionable if a previous shutdown attempt
  // was deferred by recording, display, network activity or a transient failure.
  conditions.tick(uptime());
  auto powerReading=powerMonitor.read(uptime());
  if(powerReading.valid&&conditions.active("system:low-battery")&&shutdownOnLowBattery()&&
     !pendingPower[0]&&!audioRunning&&!audioSpooling&&!displayActive()&&!streaming&&!networkActive()&&!shutdownRequested&&!bleOperation&&!zbStarting&&!workers.active()&&
     (!shutdownAttemptAt||uptime()-shutdownAttemptAt>=1000)){
    tcpService.closeAll();udpService.stop();httpApps.clear();stopWebSocket();stopBle();
    if(meshOwner==MeshOwner::Thread){OThread.end();meshOwner=MeshOwner::None;}
    shutdownAttemptAt=uptime();bool serviceStopped=true;
    if(nfsService.active()){if(auto error=nfsService.stop()){event("/services/nfs","stop_failed",error);serviceStopped=false;}else event("/services/nfs","stopped","low_battery_shutdown");}
    if(serviceStopped){JsonDocument a,r;powerHandler("shutdown",a.as<JsonObjectConst>(),r.to<JsonObject>());}
  }
  if (!audioRunning && !audioSpooling && !displayActive() && !streaming && !displayScheduler.pending() &&
      esp_clk_rtc_time() - lastRefreshRTC >= 86220000000ULL) {
    JsonDocument a, r;
    a["force"]=true;
    const char*e=nullptr;uint32_t current=presentationState.active();a["force"]=true;
    if(uiDocument(current))e=uiHandler("refresh",a.as<JsonObjectConst>(),r.to<JsonObject>());
    else if(current){a["view_id"]=current;e=displayHandler("clear",a.as<JsonObjectConst>(),r.to<JsonObject>());}
    else{auto p=new(std::nothrow)nm::Payload;if(!p)e="memory_low";else{const char*blank="{\"format\":\"scene-v1\",\"background\":1,\"commands\":[]}";p->size=strlen(blank);memcpy(p->bytes,blank,p->size);e=queueDisplay(p,E6Profile::StockUI,false,r.to<JsonObject>(),appScope,{},true,true);}}
    if (!e)
      event("/dev/display", "maintenance_clear");
  }
  core.tick();
  // Only the main loop touches Core; task producers feed a bounded queue.
  if (!pendingPower[0])
    core.dispatch(ruleCursor);
  if (usbEvents) {
    static uint32_t cursor = 0;
    JsonDocument d;
    core.events(cursor, 4, d.to<JsonObject>());
    cursor = d["cursor"] | cursor;
    if (d["events"].size()) {
      d["type"] = "events";
      serializeJson(d, Serial);
      Serial.println();
    }
  }
  if (shutdownReady.exchange(false)) {
    copy(pendingPower, sizeof(pendingPower), "shutdown");
    powerAt = millis() + 100;
  }
  commitPower();
  delay(5);
}
} // namespace notesos
