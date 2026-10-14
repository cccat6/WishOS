// Radio ownership survives Zigbee pause: this SDK suspends its task but retains radio resources.
enum class MeshOwner{None,Thread,Zigbee};static std::atomic<MeshOwner>meshOwner{MeshOwner::None};
static uint32_t threadServiceOwner=0,zigbeeServiceOwner=0;static bool threadRelease=false,zigbeeRelease=false;
static uint32_t zbTxId=0,zbTxOwner=0;
static std::atomic<bool>zbStarting{false};static std::atomic<bool>zbReady{false};static std::atomic<bool>zbTxPending{false};static uint8_t zbTxBytes[128];static esp_zb_apsde_data_req_t zbTxRequest{};static ZigbeeEP*zbEndpoint=nullptr;static uint8_t zbEndpointNumber=10;static uint16_t zbProfile=0x0104;static bool zbConsumeAps=false;
static bool zbPacket(esp_zb_apsde_data_ind_t packet){
 bool consume=zbConsumeAps&&packet.dst_endpoint==zbEndpointNumber&&packet.profile_id==zbProfile;
 JsonDocument d;d["source"]=packet.src_short_addr;d["endpoint"]=packet.src_endpoint;d["profile"]=packet.profile_id;d["cluster"]=packet.cluster_id;
 if(packet.asdu_length>128){d["bytes"]=packet.asdu_length;emit("/net/zigbee","overflow",d.as<JsonObjectConst>());return consume;}
 char data[257];hex(packet.asdu,packet.asdu_length,data);d["hex"]=data;emit("/net/zigbee","packet",d.as<JsonObjectConst>());return consume;
}
static void zbConfirm(esp_zb_apsde_data_confirm_t packet){
 bool ours=zbTxPending&&packet.dst_addr.addr_short==zbTxRequest.dst_addr.addr_short&&packet.dst_endpoint==zbTxRequest.dst_endpoint&&packet.src_endpoint==zbTxRequest.src_endpoint&&packet.asdu_length==zbTxRequest.asdu_length&&(!packet.asdu_length||(packet.asdu&&!memcmp(packet.asdu,zbTxBytes,packet.asdu_length)));
 JsonDocument d;if(ours){d["operation_id"]=zbTxId;d["owner"]=zbTxOwner;}d["own_request"]=ours;d["destination"]=packet.dst_addr.addr_short;d["endpoint"]=packet.dst_endpoint;d["status"]=packet.status;emit("/net/zigbee","tx_status",d.as<JsonObjectConst>(),ours?zbTxOwner:0);if(ours)zbTxPending=false;}
static std::atomic<bool>zbNeedsReboot{false};static std::atomic<const char*>zbStartError{nullptr};static uint32_t nextZbOperation=1;
struct ZbStart{zigbee_role_t role;bool erase,consume;uint8_t endpoint;uint16_t profile;uint32_t mask,id,owner,reservation;};
static void startZigbeeTask(void*ptr){auto p=static_cast<ZbStart*>(ptr);const char*error=nullptr;bool sdkTouched=false;
 auto endpoint=new(std::nothrow)ZigbeeEP(p->endpoint);
 if(!endpoint)error="memory_low";
 else{
  // The vendor API has no matching destroy for a partially built cluster graph.
  // After this boundary failures require reboot; retries cannot leak new graphs.
  sdkTouched=true;auto clusters=esp_zb_zcl_cluster_list_create();esp_zb_basic_cluster_cfg_t basic{};
  auto attrs=clusters?esp_zb_basic_cluster_create(&basic):nullptr;
  if(!clusters||!attrs)error="zigbee_cluster_allocation";
  else if(esp_zb_cluster_list_add_basic_cluster(clusters,attrs,ESP_ZB_ZCL_CLUSTER_SERVER_ROLE)!=ESP_OK)error="zigbee_cluster_registration";
  else{esp_zb_endpoint_config_t config{};config.endpoint=p->endpoint;config.app_profile_id=p->profile;config.app_device_id=0;config.app_device_version=0;endpoint->setEpConfig(config,clusters);
   zbEndpoint=endpoint;endpoint=nullptr; // SDK retains the endpoint even on registration failure.
   if(!Zigbee.addEndpoint(zbEndpoint))error="zigbee_endpoint_registration";
   else{zbEndpointNumber=p->endpoint;zbProfile=p->profile;zbConsumeAps=p->consume;Zigbee.setPrimaryChannelMask(p->mask);Zigbee.setTimeout(30000);
    if(!Zigbee.begin(p->role,p->erase))error="zigbee_start_failed";
    else if(!esp_zb_lock_acquire(pdMS_TO_TICKS(100)))error="zigbee_callback_registration";
    else{esp_zb_aps_data_indication_handler_register(zbPacket);esp_zb_aps_data_confirm_handler_register(zbConfirm);esp_zb_lock_release();zbReady=true;}
   }
  }
 }
 delete endpoint;zbStartError=error;zbNeedsReboot=error&&sdkTouched;if(error&&!sdkTouched)meshOwner=MeshOwner::None;
 JsonDocument result;result["operation_id"]=p->id;result["error"]=error;result["reboot_required"]=zbNeedsReboot.load();emit("/net/zigbee",error?"start_failed":"started",result.as<JsonObjectConst>(),p->owner);
 core.resources().release(p->reservation);delete p;zbStarting=false;vTaskDelete(nullptr);
}
static const char*zigbeeHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"start")){
   if(zbNeedsReboot)return "zigbee_reboot_required";
   if(notes_radio_boot_mode()!=2)return "zigbee_requires_boot_selection";
   if(meshOwner==MeshOwner::Thread)return "radio_owned_by_thread";if(meshOwner==MeshOwner::Zigbee)return "use_resume_or_reboot_to_reconfigure";
   const char*role=a["role"]|"router";zigbee_role_t r;if(!strcmp(role,"router"))r=ZIGBEE_ROUTER;else if(!strcmp(role,"coordinator"))r=ZIGBEE_COORDINATOR;else return "zigbee_role";
   uint32_t ep=a["endpoint"]|10u,profile=a["profile"]|0x0104u,mask=a["channel_mask"]|0x07fff800u;
   if(!ep||ep>240||profile>65535||!mask||(mask&~0x07fff800u))return "zigbee_configuration";
   if(!esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"zb_storage")||!esp_partition_find_first(ESP_PARTITION_TYPE_DATA,ESP_PARTITION_SUBTYPE_ANY,"zb_fct"))return "zigbee_partitions_required";
   auto reservation=core.resources().acquire(sizeof(ZbStart)+8192,core.callerOwner());if(!reservation)return "zigbee_memory_budget";
   auto job=new(std::nothrow)ZbStart{r,a["erase_network"]|false,a["consume_aps"]|false,uint8_t(ep),uint16_t(profile),mask,nextZbOperation++,core.callerOwner(),reservation};
   if(!job){core.resources().release(reservation);return "memory_low";}auto id=job->id;meshOwner=MeshOwner::Zigbee;zbStarting=true;
   if(xTaskCreate(startZigbeeTask,"notes-zb-start",6144,job,1,nullptr)!=pdPASS){delete job;core.resources().release(reservation);zbStarting=false;meshOwner=MeshOwner::None;return "zigbee_start_task_failed";}
   zigbeeServiceOwner=core.callerOwner();out["operation_id"]=id;out["state"]="starting";return nullptr;
 }
 if(strcmp(m,"read")&&meshOwner!=MeshOwner::Zigbee)return "zigbee_not_started";if(zbStarting&&strcmp(m,"read"))return "zigbee_starting";
 if(!strcmp(m,"pause")){if(zbTxPending)return "zigbee_tx_pending";Zigbee.stop();}
 else if(!strcmp(m,"resume")){if(!zbReady)return "zigbee_not_ready";Zigbee.start();}
 else if(!strcmp(m,"permit_join")){uint32_t seconds=a["seconds"]|0u;if(seconds>254)return "join_window_range";if(!Zigbee.connected())return "zigbee_not_joined";Zigbee.openNetwork(seconds);}
 else if(!strcmp(m,"send")){
   uint8_t payload[128];size_t n;uint32_t address=a["address"]|0xffffu,endpoint=a["endpoint"]|0u,profile=a["profile"]|0x0104u,cluster=a["cluster"]|0u;
   if(address>65535||!endpoint||endpoint>240||profile>65535||cluster>65535||!unhex(a["hex"]|"",payload,sizeof payload,n))return "zigbee_packet_range";
   if(!Zigbee.connected())return "zigbee_not_joined";if(zbTxPending)return "zigbee_tx_pending";zbTxRequest={};auto&req=zbTxRequest;req.dst_addr_mode=2;req.dst_addr.addr_short=address;req.dst_endpoint=endpoint;req.src_endpoint=zbEndpointNumber;req.profile_id=profile;req.cluster_id=cluster;memcpy(zbTxBytes,payload,n);req.asdu=zbTxBytes;req.asdu_length=n;req.radius=10;
   if(!esp_zb_lock_acquire(pdMS_TO_TICKS(10)))return "zigbee_busy";zbTxId=nextZbOperation++;zbTxOwner=core.callerOwner();zbTxPending=true;auto e=esp_zb_aps_data_request(&req);esp_zb_lock_release();if(e!=ESP_OK){zbTxPending=false;return "zigbee_send_failed";}out["state"]="queued";out["operation_id"]=zbTxId;return nullptr;
 }else if(strcmp(m,"read")&&strcmp(m,"pause")&&strcmp(m,"resume")&&strcmp(m,"permit_join"))return "unsupported_method";
 out["owner"]=zigbeeServiceOwner;out["sdk_lifetime"]="until_reboot";out["reboot_required"]=zbNeedsReboot.load();out["start_error"]=zbStartError.load();out["starting"]=zbStarting.load();out["running"]=meshOwner==MeshOwner::Zigbee&&!zbStarting&&Zigbee.started();out["connected"]=meshOwner==MeshOwner::Zigbee&&!zbStarting&&Zigbee.connected();if(zbStarting)out["endpoint"]=nullptr;else out["endpoint"]=zbEndpointNumber;out["tx_pending"]=zbTxPending.load();if(zbStarting)out["consume_aps"]=nullptr;else out["consume_aps"]=zbConsumeAps;out["release_radio"]="reboot_after_zigbee";return nullptr;
}
static const char*threadNativeHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"start")){
   if(notes_radio_boot_mode()!=1)return "thread_requires_boot_selection";
   if(meshOwner==MeshOwner::Zigbee)return "radio_owned_by_zigbee_reboot_required";if(meshOwner==MeshOwner::Thread)return "thread_already_started";
   otOperationalDatasetTlvs dataset{};size_t n=0;const char*hexdata=a["dataset_hex"]|"";
   if(*hexdata&&(!unhex(hexdata,dataset.mTlvs,sizeof dataset.mTlvs,n)||!n))return "thread_dataset_invalid";dataset.mLength=n;
   OThread.begin(false);if(!OThread)return "thread_init_failed";meshOwner=MeshOwner::Thread;
   if(n){if(!esp_openthread_lock_acquire(pdMS_TO_TICKS(50))){OThread.end();meshOwner=MeshOwner::None;return "thread_busy";}auto e=otDatasetSetActiveTlvs(OThread.getInstance(),&dataset);esp_openthread_lock_release();if(e!=OT_ERROR_NONE){OThread.end();meshOwner=MeshOwner::None;return "thread_dataset_invalid";}}
   if(!OThread.hasActiveDataset()){OThread.end();meshOwner=MeshOwner::None;return "thread_dataset_required";}
   OThread.networkInterfaceUp();OThread.start();event("/net/thread","started");
 }else if(!strcmp(m,"stop")){if(meshOwner==MeshOwner::Thread){OThread.end();meshOwner=MeshOwner::None;event("/net/thread","stopped");}}
 else if(strcmp(m,"read"))return "unsupported_method";
 out["running"]=meshOwner==MeshOwner::Thread; if(meshOwner==MeshOwner::Thread){out["role"]=OThread.otGetStringDeviceRole();out["network_name"]=OThread.getNetworkName();out["channel"]=OThread.getChannel();out["mesh_local_address"]=OThread.getMeshLocalEid().toString();}return nullptr;
}

static JsonDocument threadParameters;static bool threadStopping=false;static char threadLastError[80]{};
struct ThreadSnapshot{char role[16]{},name[17]{},address[64]{};unsigned channel=0;uint64_t at=0;};static ThreadSnapshot threadSnapshot;
struct ThreadTaskEntry{void(*run)(void*);void*context;};static ThreadTaskEntry threadTaskEntry;
static NativeOperation threadOperation(core,"/net/thread",{
 [](void(*run)(void*),void*context){threadTaskEntry={run,context};return xTaskCreate([](void*){auto e=threadTaskEntry;e.run(e.context);vTaskDelete(nullptr);},"notes-thread-life",6144,nullptr,1,nullptr)==pdPASS;},8192},nullptr,
 [](void*,const std::atomic<bool>&cancel)->const char*{if(cancel)return "cancelled";JsonDocument out;return threadNativeHandler(threadStopping?"stop":"start",threadParameters.as<JsonObjectConst>(),out.to<JsonObject>());},
 [](void*){threadParameters.clear();JsonDocument out;threadOperation.read(out.to<JsonObject>());copy(threadLastError,sizeof threadLastError,out["error"]|"");if(meshOwner!=MeshOwner::Thread)threadSnapshot={};});
static const char*threadHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"start")||!strcmp(m,"stop")){
  if(threadOperation.active())return "thread_operation_active";for(auto f:a)if(strcmp(m,"start")||strcmp(f.key().c_str(),"dataset_hex"))return "unknown_thread_argument";
  if(!strcmp(m,"start")){if(notes_radio_boot_mode()!=1)return "thread_requires_boot_selection";if(meshOwner!=MeshOwner::None)return "radio_owned";
   if(a.containsKey("dataset_hex")){if(!a["dataset_hex"].is<const char*>()||a["dataset_hex"].as<JsonString>().size()!=strlen(a["dataset_hex"].as<const char*>()))return "thread_dataset_invalid";otOperationalDatasetTlvs shape{};uint8_t bytes[sizeof shape.mTlvs];size_t n=0;if(!unhex(a["dataset_hex"],bytes,sizeof bytes,n))return "thread_dataset_invalid";}}
  threadParameters.set(a);if(threadParameters.overflowed())return "memory_low";threadStopping=!strcmp(m,"stop");auto error=threadOperation.start(m,out);if(!error&&!threadStopping)threadServiceOwner=core.callerOwner();return error;
 }
 if(strcmp(m,"read"))return "unsupported_method";out["owner"]=threadServiceOwner;out["operation_active"]=threadOperation.active();out["running"]=meshOwner==MeshOwner::Thread;out["last_error"]=threadLastError;if(threadSnapshot.at){out["role"]=threadSnapshot.role;out["network_name"]=threadSnapshot.name;out["channel"]=threadSnapshot.channel;out["mesh_local_address"]=threadSnapshot.address;out["sampled_at_ms"]=threadSnapshot.at;}return nullptr;
}
static void pollThread(){threadOperation.poll();if(threadRelease&&!threadOperation.active()){threadRelease=false;if(meshOwner==MeshOwner::Thread){JsonDocument out;threadHandler("stop",JsonObjectConst(),out.to<JsonObject>());}threadServiceOwner=0;}if(threadOperation.active()||meshOwner!=MeshOwner::Thread)return;
 if(!esp_openthread_lock_acquire(0))return;auto instance=OThread.getInstance();auto role=otThreadGetDeviceRole(instance);const char*names[]={"disabled","detached","child","router","leader"};copy(threadSnapshot.role,sizeof threadSnapshot.role,unsigned(role)<5?names[unsigned(role)]:"unknown");copy(threadSnapshot.name,sizeof threadSnapshot.name,otThreadGetNetworkName(instance));threadSnapshot.channel=otLinkGetChannel(instance);auto address=otThreadGetMeshLocalEid(instance);if(address)inet_ntop(AF_INET6,address->mFields.m8,threadSnapshot.address,sizeof threadSnapshot.address);threadSnapshot.at=uptime();esp_openthread_lock_release();
}

static const char*radioHandler(const char*m,JsonObjectConst a,JsonObject out){
 if(!strcmp(m,"write")){const char*p=a["protocol"]|"";uint8_t mode;if(!strcmp(p,"off"))mode=0;else if(!strcmp(p,"thread"))mode=1;else if(!strcmp(p,"zigbee"))mode=2;else return "radio_protocol";
   if(preferences.putUChar("mesh_boot",mode)!=1)return "persistence_failed";}
 else if(strcmp(m,"read"))return "unsupported_method";
 uint8_t actual=notes_radio_boot_mode(),desired=preferences.getUChar("mesh_boot",1);
 out["active_protocol"]=actual==1?"thread":actual==2?"zigbee":"off";
 out["configured_protocol"]=desired==1?"thread":desired==2?"zigbee":"off";
 out["reboot_required"]=actual!=desired;out["apply_time"]="next_boot";out["simultaneous_802154_stacks"]=false;
 auto options=out["protocols"].to<JsonArray>();options.add("off");options.add("thread");options.add("zigbee");
 JsonDocument state,empty;wifiHandler("read",empty.as<JsonObjectConst>(),state.to<JsonObject>());auto wifi=out["wifi"].to<JsonObject>();wifi["control"]="/dev/wifi";for(auto key:{"connected","ssid","selection","configuration_pending","connection_pending","reason","radio_mode"})wifi[key]=state[key];wifi["enabled"]=state["enabled"];wifi["configured_enabled"]=state["configured"]["settings"]["enabled"];wifi["auto_connect"]=state["configured"]["settings"]["auto_connect"];
 state.clear();bleHandler("read",empty.as<JsonObjectConst>(),state.to<JsonObject>());auto ble=out["ble"].to<JsonObject>();ble["control"]="/dev/ble";for(auto key:{"running","mode","connections","restart_required"})ble[key]=state[key];ble["auto_start"]=state["next_boot_auto_start"];ble["configured_auto_start"]=state["configured"]["auto_start"];
 auto mesh=out["ieee802154"].to<JsonObject>();mesh["control"]="/sys/radio";mesh["active_protocol"]=out["active_protocol"];mesh["configured_protocol"]=out["configured_protocol"];mesh["reboot_required"]=out["reboot_required"];mesh["apply_time"]="next_boot";
 out["configuration_model"]="independent_subsystems";return nullptr;
}

static void registerMeshOwnership(){core.onOwnerRelease(&meshOwner,[](void*,uint32_t owner){if(threadServiceOwner==owner)threadRelease=true;if(zigbeeServiceOwner==owner)zigbeeRelease=true;});}
static void pollMesh(){
 pollThread();if(zigbeeRelease&&!zbStarting&&!zbTxPending){zigbeeRelease=false;if(meshOwner==MeshOwner::Zigbee&&Zigbee.started())Zigbee.stop();zigbeeServiceOwner=0;}
 if(meshOwner==MeshOwner::Zigbee&&!zbStarting&&!zbReady&&!zbNeedsReboot&&Zigbee.started()){
   if(!esp_zb_lock_acquire(0))return;esp_zb_aps_data_indication_handler_register(zbPacket);esp_zb_aps_data_confirm_handler_register(zbConfirm);esp_zb_lock_release();zbReady=true;event("/net/zigbee","started");
 }
}
