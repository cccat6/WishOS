// This finished device's battery circuit is characterized by its upstream raw-ADC
// table. Keep the estimate with its composition, rather than an MCU-wide scale.
static adc_oneshot_unit_handle_t batteryAdc=nullptr;
static adc_channel_t batteryChannel=ADC_CHANNEL_0;
static unsigned batteryRaw=0,batteryLevel=0;
static std::atomic<unsigned>batteryPendingRaw{0};
static bool batteryValid=false,batteryCharging=false;
static uint64_t batterySampled=0,nextPowerSample=0;static uint32_t batterySampleInterval=30000;
static char batteryError[80]{};
struct BatterySampleTask{void(*run)(void*);void*context;};
static void batterySampleTask(void*p){auto call=*static_cast<BatterySampleTask*>(p);delete static_cast<BatterySampleTask*>(p);call.run(call.context);vTaskDelete(nullptr);}
static NativeOperation batterySample(core,"/dev/battery",{[](void(*run)(void*),void*p){auto e=new(std::nothrow)BatterySampleTask{run,p};if(!e)return false;if(xTaskCreate(batterySampleTask,"wish-battery",3072,e,1,nullptr)!=pdPASS){delete e;return false;}return true;},4096},nullptr,[](void*,const std::atomic<bool>&cancel)->const char*{
 if(cancel)return "cancelled";if(!batteryAdc){adc_unit_t unitId;auto resolved=adc_oneshot_io_to_channel(pins::BATTERY_ADC,&unitId,&batteryChannel);if(resolved!=ESP_OK)return esp_err_to_name(resolved);adc_oneshot_unit_init_cfg_t unit{};unit.unit_id=unitId;unit.ulp_mode=ADC_ULP_MODE_DISABLE;auto e=adc_oneshot_new_unit(&unit,&batteryAdc);if(e!=ESP_OK)return esp_err_to_name(e);adc_oneshot_chan_cfg_t channel{};channel.atten=ADC_ATTEN_DB_12;channel.bitwidth=ADC_BITWIDTH_12;e=adc_oneshot_config_channel(batteryAdc,batteryChannel,&channel);if(e!=ESP_OK){adc_oneshot_del_unit(batteryAdc);batteryAdc=nullptr;return esp_err_to_name(e);}}
 int value=0;auto e=adc_oneshot_read(batteryAdc,batteryChannel,&value);if(e!=ESP_OK)return esp_err_to_name(e);batteryPendingRaw=value;return nullptr;
},[](void*){JsonDocument state;batterySample.read(state.to<JsonObject>());copy(batteryError,sizeof batteryError,state["error"]|"");if(!*batteryError){batteryRaw=batteryPendingRaw.load();batteryLevel=batteryRaw<1970?0:batteryRaw>=2430?100:(batteryRaw-1970)*100/460;batteryValid=true;batterySampled=uptime();JsonDocument sample;sample["battery_percent"]=batteryLevel;sample["raw_adc"]=batteryRaw;sample["sampled_at_ms"]=batterySampled;core.emit("/dev/battery","sample",sample.as<JsonObjectConst>());}batterySampleInterval=policies.get<uint32_t>("/dev/battery","sample_interval_ms",30000);nextPowerSample=uptime()+batterySampleInterval;});
static const char* startPowerSample(JsonObject out){return batterySample.start("measure",out);}
static void startPowerSample(){JsonDocument out;startPowerSample(out.to<JsonObject>());}
static void pollPower(){batterySample.poll();bool charging=digitalRead(pins::CHARGE_SENSE)==HIGH;if(charging!=batteryCharging){batteryCharging=charging;JsonDocument event;event["charging"]=charging;core.emit("/dev/battery","charging_changed",event.as<JsonObjectConst>());}if(!batterySample.active()&&uptime()>=nextPowerSample)startPowerSample();}
static void batteryDescription(JsonObject out){
 out["transport"]="adc_and_charger_gpio";out["percentage_basis"]="upstream_device_raw_adc_curve";
 for(auto key:{"valid","charging","sampling"}){auto property=out["properties"][key].to<JsonObject>();property["type"]="boolean";property["access"]="r";}
 for(auto key:{"battery_percent","raw_adc","sampled_at_ms","sample_interval_ms","applied_sample_interval_ms","next_sample_at_ms"}){auto property=out["properties"][key].to<JsonObject>();property["type"]="integer";property["access"]="r";property["unit"]=!strcmp(key,"battery_percent")?"percent":!strcmp(key,"raw_adc")?"raw_count":"ms";}
 out["properties"]["battery_percent"]["minimum"]=0;out["properties"]["battery_percent"]["maximum"]=100;out["properties"]["battery_percent"]["estimate"]=true;
 out["configuration_schema"]="/sys/config describe";out["configuration_resource"]="/dev/battery";out["configuration_apply_time"]="next_sample";
 out["events"]="sample,charging_changed,operation_completed,operation_failed";NativeOperation::describe(out,"measure","/dev/battery");
}
static void registerPower(){core.add({"/dev/battery","read,describe,measure,cancel","battery and charger input",[](const char*m,JsonObjectConst a,JsonObject out)->const char*{
 if(!strcmp(m,"cancel"))return batterySample.cancel(a);if(!strcmp(m,"measure"))return startPowerSample(out);
 if(!strcmp(m,"describe")){batteryDescription(out);return nullptr;}
 if(strcmp(m,"read"))return "unsupported_method";out["valid"]=batteryValid;out["charging"]=batteryCharging;out["sampled_at_ms"]=batterySampled;out["_meta"]["sampled_at_ms"]=batterySampled;out["sample_interval_ms"]=policies.get<uint32_t>("/dev/battery","sample_interval_ms",30000);out["applied_sample_interval_ms"]=batterySampleInterval;out["next_sample_at_ms"]=nextPowerSample;out["raw_adc"]=batteryRaw;if(batteryValid)out["battery_percent"]=batteryLevel;out["error"]=batteryError;out["sampling"]=batterySample.active();return nullptr;
 },batteryDescription,"read,describe"});}
