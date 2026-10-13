static bool scriptAdmission(size_t budget){return core.resources().canAllocate(budget);}
static ScriptHost scriptHost(core,vfs,scriptAdmission);
static bool scriptEvent(const Event&e){return scriptHost.event(e);}
static bool persistRuntime(const char*text,size_t n){return preferences.putString("runtime_start",text)==n;}
static const char*scriptHandler(const char*m,JsonObjectConst a,JsonObject out){return scriptHost.invoke(m,a,out,persistRuntime);}
static void restoreRuntime(){
 String text=preferences.getString("runtime_start","");
 if(!text.length()&&*boardpack::STARTUP_SCRIPT){JsonDocument d;d["file"]=boardpack::STARTUP_SCRIPT;d["auto_start"]=true;d["memory_bytes"]=32768;d["instruction_budget"]=20000;d["delay_ms"]=1000;serializeJson(d,text);scriptHost.restore(text.c_str());}
 else if(text.length())scriptHost.restore(text.c_str());
 scriptHost.scheduleBoot(uptime());
}
