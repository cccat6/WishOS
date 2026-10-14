// Device-selected package references. Every launcher/voice program uses the same
// ScriptHost as an installed user application; no native page implementation.
static const char*packageApplicationId(const char*key){static char launcher[49]{},voice[49]{};char*result=!strcmp(key,"launcher")?launcher:voice;if(*result)return result;for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")){JsonDocument config;if(!deserializeJson(config,asset.data,asset.size))copy(result,49,config[key]|"");break;}return result;}
static Applications applications(core,vfs,scriptHost,packageApplicationId("launcher"),packageApplicationId("voice"));
static bool saveApplications(const char*text,size_t size){return preferences.putString("applications",text)==size;}
static const char*applicationsHandler(const char*m,JsonObjectConst a,JsonObject o){return applications.invoke(m,a,o,saveApplications);}
static bool applicationsReady=false;
static void restoreApplications(){if(resourceStartupError){applicationsReady=true;return;}for(auto asset:boardpack::ASSETS)if(!strcmp(asset.path,"/applications.json")){JsonDocument config;String records;if(!deserializeJson(config,asset.data,asset.size)){serializeJson(config["applications"],records);applications.defaults(records.c_str());}break;}String saved=preferences.getString("applications","");if(saved.length())applications.restore(saved.c_str());applicationsReady=false;}
static void pollApplications(){if(!applicationsReady){applicationsReady=true;JsonDocument state;scriptHost.invoke("read",JsonObjectConst(),state.to<JsonObject>());if(!applications.transitionPending()&&!(state["loaded"]|false)&&applications.hasLauncher())applications.requestHome();}applications.poll();}
