static ScriptHost scriptHost(core,vfs);
static bool scriptEvent(const Event&e){return scriptHost.event(e);}
static bool persistRuntime(const char*text,size_t n){return preferences.putString("runtime_start",text)==n;}
static const char*scriptHandler(const char*m,JsonObjectConst a,JsonObject out){return scriptHost.invoke(m,a,out,persistRuntime);}
static void restoreRuntime(){
 if(resourceStartupError){JsonDocument error;error["error"]=resourceStartupError;core.emit("/sys/resources","configuration_rejected",error.as<JsonObjectConst>());return;}
 String text=preferences.getString("runtime_start","");
 if(!text.length()&&*boardpack::STARTUP_SCRIPT){JsonDocument d;d["file"]=boardpack::STARTUP_SCRIPT;d["auto_start"]=true;if(boardpack::STARTUP_MEMORY_BYTES)d["memory_bytes"]=boardpack::STARTUP_MEMORY_BYTES;else d["memory_bytes"]=nullptr;d["instruction_budget"]=20000;d["system_call_budget"]=32;d["delay_ms"]=1000;serializeJson(d,text);scriptHost.restore(text.c_str());}
 else if(text.length())scriptHost.restore(text.c_str());
 scriptHost.scheduleBoot(uptime());
}
