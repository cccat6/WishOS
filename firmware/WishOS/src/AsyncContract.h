#pragma once
#include <ArduinoJson.h>
#include <initializer_list>
namespace notesos {
// Method completion is driver metadata, independent of the scripting language.
// Mapping keys name event.data fields; values name fields in the accepted reply.
inline JsonObject asyncContract(JsonObject out,const char*method,const char*source,
 std::initializer_list<const char*>events={"completed","failed"}){
 auto spec=out["async"][method].to<JsonObject>();spec["source"]=source;
 auto success=spec["success_events"].to<JsonArray>();if(events.size())success.add(*events.begin());
 auto names=spec["events"].to<JsonArray>();for(auto event:events)names.add(event);names.add("delivery_error");
 return spec["match"].to<JsonObject>();
}
inline void workerAsyncDescription(JsonObject out){asyncContract(out,"submit","/sys/workers")["id"]="id";}
inline void remoteAsyncDescription(JsonObject out){
 asyncContract(out,"submit","/services/remote")["id"]="id";
 auto settled=out["async"]["submit"]["settled"].to<JsonObject>();settled["field"]="state";
 settled["events"]["completed"]="completed";settled["events"]["failed"]="failed";
}
inline void httpAsyncDescription(JsonObject out){asyncContract(out,"request","/net/http")["job_id"]="job_id";out["job_capacity"]=1;out["result_lifetime"]="explicit_release_or_owner_exit";out["job_id_required_for"]="receive,cancel,release";out["result_lookup"]="read with job_id";}
inline void wsAsyncDescription(JsonObject o){asyncContract(o,"connect","/net/ws",{"connected","failed"})["connection_id"]="connection_id";asyncContract(o,"send","/net/ws",{"sent","send_failed"})["message_id"]="message_id";o["execution"]="background_transport_task";o["close_completion"]="closed event";}
inline void uiAsyncDescription(JsonObject out){for(auto method:{"open","update","refresh"}){auto match=asyncContract(out,method,"/dev/display");match["view_id"]="view_id";match["view_revision"]="revision";}}
}
