#pragma once
#include "Core.h"
namespace notesos {
// Transport/session metadata is validated once, before driver dispatch. It is
// never forwarded as a device setting or left to individual drivers to enforce.
inline void invokeEnvelope(Core&core,JsonObjectConst request,JsonDocument&document,const char*boot,uint64_t now){
 auto reply=document.to<JsonObject>();reply["id"]=request["id"];reply["boot_id"]=boot;reply["at_ms"]=now;auto result=reply["result"].to<JsonObject>();
 const char*error=nullptr;JsonDocument args;bool dispatched=false;
 for(auto name:{"path","method"})if(request.containsKey(name)&&(!request[name].is<const char*>()||request[name].as<JsonString>().size()!=strlen(request[name].as<const char*>())))error="request_field_type";
 if(!request["args"].isNull()&&!request["args"].is<JsonObjectConst>())error="arguments_object_required";
 if(!error){args.set(request["args"].isNull()?JsonVariantConst():request["args"]);if(args.isNull())args.to<JsonObject>();if(args.overflowed())error="memory_low";}
 for(auto token:{request["expected_boot_id"],request["args"]["expected_boot_id"]})if(!token.isNull()){
  if(!token.is<const char*>()||token.as<JsonString>().size()!=strlen(token.as<const char*>())){error="boot_id_type";break;}if(strcmp(token.as<const char*>(),boot)){error="boot_changed";break;}
 }
 if(!error){args.remove("expected_boot_id");dispatched=true;error=core.invoke(request["path"]|"",request["method"]|"read",args.as<JsonObjectConst>(),result);}
 reply["ok"]=!error;if(error)reply["error"]=error;
 if(document.overflowed()||measureJson(document)>MAX_WIRE){document.clear();reply=document.to<JsonObject>();reply["id"]=request["id"];reply["boot_id"]=boot;reply["ok"]=false;reply["error"]="reply_limit";reply["dispatched"]=dispatched;}
}
}
