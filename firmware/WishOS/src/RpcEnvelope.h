#pragma once
#include "Core.h"
namespace notesos {
// Transport/session metadata is validated once, before driver dispatch. It is
// never forwarded as a device setting or left to individual drivers to enforce.
inline void invokeEnvelope(Core&core,JsonObjectConst request,JsonObject reply,const char*boot,uint64_t now){
 reply["id"]=request["id"];reply["boot_id"]=boot;reply["at_ms"]=now;auto result=reply["result"].to<JsonObject>();
 const char*error=nullptr;JsonDocument args;
 if(!request["args"].isNull()&&!request["args"].is<JsonObjectConst>())error="arguments_object_required";
 if(!error){args.set(request["args"].isNull()?JsonVariantConst():request["args"]);if(args.isNull())args.to<JsonObject>();if(args.overflowed())error="memory_low";}
 for(auto token:{request["expected_boot_id"],request["args"]["expected_boot_id"]})if(!token.isNull()){
  if(!token.is<const char*>()){error="boot_id_type";break;}if(strcmp(token.as<const char*>(),boot)){error="boot_changed";break;}
 }
 if(!error){args.remove("expected_boot_id");error=core.invoke(request["path"]|"",request["method"]|"read",args.as<JsonObjectConst>(),result);}
 reply["ok"]=!error;if(error)reply["error"]=error;
}
}
