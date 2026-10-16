// Device-selected package references. Every launcher/voice program uses the same
// ScriptHost as an installed user application; no native page implementation.
static const char*packageApplicationId(const char*key){static char launcher[49]{},voice[49]{};char*result=!strcmp(key,"launcher")?launcher:voice;if(*result)return result;for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")){JsonDocument config;if(!deserializeJson(config,asset.data,asset.size))copy(result,49,config[key]|"");break;}return result;}
static constexpr size_t applicationCatalogBytes=8192;
static Applications applications(core,vfs,scriptHost,packageApplicationId("launcher"),packageApplicationId("voice"),applicationCatalogBytes,boardpack::APPLICATION_PROVIDERS);
static const char*scriptHandler(const char*m,JsonObjectConst a,JsonObject out){return applications.runtimeInvoke(m,a,out);}
// Persist compressed catalog metadata, not application code. LZ4 keeps atomic
// NVS replacement from needing two large, mostly repeated JSON strings at once.
static bool saveApplications(const char*text,size_t size){
 size_t capacity=nm::lz4Bound(size)+8;auto encoded=static_cast<uint8_t*>(core.resources().allocate(capacity));if(!encoded)return false;
 size_t count=nm::lz4Pack(reinterpret_cast<const uint8_t*>(text),size,encoded+8,capacity-8);bool packed=count&&count+8<size;
 if(packed){memcpy(encoded,"WAC1",4);for(unsigned i=0;i<4;++i)encoded[4+i]=uint8_t(size>>(8*i));}
 size_t bytes=packed?count+8:size;bool ok=preferences.putBytes("app_catalog",packed?static_cast<const void*>(encoded):text,bytes)==bytes;core.resources().freeAllocation(encoded);
 if(ok)preferences.remove("applications");return ok;
}
static void restoreApplicationCatalog(){
 if(preferences.getType("app_catalog")!=PT_BLOB){String saved=preferences.getString("applications","");if(saved.length())applications.restore(saved.c_str());return;}
 size_t size=preferences.getBytesLength("app_catalog");auto bytes=size&&size<=applicationCatalogBytes?static_cast<uint8_t*>(core.resources().allocate(size+1)):nullptr;
 if(!bytes){applications.restore("{}");return;}bool valid=preferences.getBytes("app_catalog",bytes,size)==size;bytes[size]=0;
 if(valid&&size>=8&&!memcmp(bytes,"WAC1",4)){uint32_t length=0;for(unsigned i=0;i<4;++i)length|=uint32_t(bytes[4+i])<<(8*i);auto text=length&&length<=applicationCatalogBytes?static_cast<uint8_t*>(core.resources().allocate(length+1)):nullptr;
  if(text){text[length]=0;applications.restore(nm::lz4Raw(bytes+8,size-8,text,length)?reinterpret_cast<char*>(text):"{}");core.resources().freeAllocation(text);}else applications.restore("{}");
 }else applications.restore(valid?reinterpret_cast<char*>(bytes):"{}");core.resources().freeAllocation(bytes);
}
static const char*applicationsHandler(const char*m,JsonObjectConst a,JsonObject o){return applications.invoke(m,a,o,saveApplications);}
static bool applicationsReady=false;
static void restoreApplications(){if(resourceStartupError){applicationsReady=true;return;}for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")){JsonDocument config;String records;if(!deserializeJson(config,asset.data,asset.size)){auto entries=config["applications"].as<JsonArray>();if(*boardpack::RUNTIME_PATH)for(auto entry:entries){String seed=entry["manifest"].as<const char*>();if(!entry["fallback_manifest"].is<const char*>())entry["fallback_manifest"]=seed;entry["manifest"]=String(boardpack::RUNTIME_PATH)+seed.substring(7);}serializeJson(entries,records);applications.defaults(records.c_str());}break;}restoreApplicationCatalog();applicationsReady=false;}
static void pollApplications(){if(!applicationsReady){applicationsReady=true;JsonDocument state;scriptHost.invoke("read",JsonObjectConst(),state.to<JsonObject>());if(!applications.transitionPending()&&!(state["loaded"]|false)&&applications.hasLauncher())applications.requestHome();}applications.poll();}
