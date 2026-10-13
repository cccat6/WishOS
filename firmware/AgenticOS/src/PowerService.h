// The board driver conditions its raw ADC internally. Capabilities are declared
// separately; all higher-level conditions use the shared system evaluator.
static PowerMonitor powerMonitor(pins::VBUS_CLAMPS_BATTERY_ADC,pins::BATTERY_DIVIDER);
static uint32_t powerPolicyRevision=0,powerEventMs=1000;
static uint64_t powerSampleAt=0,powerPublishAt=0,shutdownAttemptAt=0;
static bool powerSampled=false,powerPublished=false;
static void syncPowerPolicy(){
  if(powerPolicyRevision==policies.revision())return;
  JsonDocument d;policies.resolve("/dev/battery","system",d.to<JsonObject>());
  PowerSenseConfig cfg;cfg.sampleMs=d["sample_ms"]|100u;cfg.recoveryGuardMs=d["recovery_guard_ms"]|1000u;
  cfg.stableWindowMs=d["stable_window_ms"]|500u;cfg.stableToleranceMv=d["stable_tolerance_mv"]|20u;
  cfg.presenceDebounceMs=d["external_power_debounce_ms"]|200u;powerEventMs=d["event_interval_ms"]|1000u;powerMonitor.configure(cfg);
  // System energy policy uses exactly the same condition mechanism as applications.
  d.clear();policies.resolve("/sys/power","system",d.to<JsonObject>());
  static uint32_t lowMv=0,lowHysteresis=0,lowHold=0;
  uint32_t mv=d["low_battery_mv"]|3550u,hysteresis=d["low_battery_hysteresis_mv"]|100u,hold=d["low_battery_hold_ms"]|3000u;
  if(!powerPolicyRevision||mv!=lowMv||hysteresis!=lowHysteresis||hold!=lowHold){
    JsonDocument a,r;a["id"]="system:low-battery";a["source"]="/dev/battery";a["property"]="mv";a["operator"]="below";a["value"]=mv;a["hysteresis"]=hysteresis;a["hold_ms"]=hold;a["emit_initial"]=true;
    if(conditions.set(a.as<JsonObjectConst>(),r.to<JsonObject>(),true))return;
    lowMv=mv;lowHysteresis=hysteresis;lowHold=hold;
  }
  powerPolicyRevision=policies.revision();
}
static void powerValues(const PowerReading&s,JsonObject out,bool externalSense=powerMonitor.shared()){
  if(s.valid)out["mv"]=s.batteryMv;else out["mv"]=nullptr;
  if(externalSense){if(s.externalPower<0)out["external_power"]=nullptr;else out["external_power"]=bool(s.externalPower);}
  out["_meta"]["sampled_at_ms"]=s.atMs;
}
static const char*batteryHandler(const char*m,JsonObjectConst a,JsonObject out){
  syncPowerPolicy();
  if(!strcmp(m,"write")){
    for(auto field:a)if(strcmp(field.key().c_str(),"calibration_gain"))return "unknown_driver_setting";
    if(!a["calibration_gain"].isNull()&&!a["calibration_gain"].is<double>())return "calibration_gain_type";
    float gain=a["calibration_gain"]|batteryGain;if(!isfinite(gain)||gain<0.8f||gain>1.2f)return "battery_calibration_range";
    if(gain!=batteryGain){batteryGain=gain;powerMonitor.reset();}
  }else if(strcmp(m,"read"))return "unsupported_method";
  powerValues(powerMonitor.read(uptime()),out);out["calibration_gain"]=batteryGain;return nullptr;
}
static void pollPowerSense(){
  syncPowerPolicy();uint64_t now=uptime();if(powerSampled&&now-powerSampleAt<powerMonitor.config().sampleMs)return;powerSampleAt=now;powerSampled=true;
  auto before=powerMonitor.read(now);auto transition=powerMonitor.update(now,readBatteryAdcMv(),batteryGain);auto s=powerMonitor.read(now);
  bool changed=before.valid!=s.valid || (before.valid&&s.valid&&before.batteryMv!=s.batteryMv) || transition.changed;
  if(changed||!powerPublished||now-powerPublishAt>=powerEventMs){
    JsonDocument d;powerValues(s,d.to<JsonObject>());emit("/dev/battery",changed?"changed":"sample",d.as<JsonObjectConst>());powerPublished=true;powerPublishAt=now;
  }
}
