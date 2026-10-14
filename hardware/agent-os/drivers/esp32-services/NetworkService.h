struct NetworkChunk{uint16_t length=0;uint8_t data[512]{};};
struct NetworkJob {
  std::atomic<bool> busy{true},done{false},cancel{false};uint32_t id=0,owner=0,reservation=0,timeout=10000;unsigned limit=4096;
  char url[1024]{},method[8]{},body[2048]{},headers[1024]{},response[4097]{},error[64]{};
  QueueHandle_t chunks=nullptr;bool stream=false,readableNotified=false,orphan=false;
  char*ca=nullptr;int status=0;size_t received=0;
  ~NetworkJob(){if(reservation)core.resources().release(reservation);free(ca);if(chunks)vQueueDelete(chunks);}
};
static NetworkJob* networkJob=nullptr;static bool networkNotified=false;
static bool networkActive(){return networkJob&&(networkJob->busy||(networkJob->chunks&&uxQueueMessagesWaiting(networkJob->chunks)));}
class ResponseSink:public Stream {
 NetworkJob&j_;
public:
 bool overflow=false,binary=false;explicit ResponseSink(NetworkJob&j):j_(j){}
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}void flush()override{}
 size_t write(uint8_t b)override{return write(&b,1);}
 size_t write(const uint8_t*p,size_t n)override{if(j_.cancel.load())return 0;if(n>j_.limit-j_.received){overflow=true;return 0;}
 if(j_.stream){size_t at=0;while(at<n){NetworkChunk chunk;chunk.length=std::min(size_t(512),n-at);memcpy(chunk.data,p+at,chunk.length);
   uint32_t began=millis();while(xQueueSend(j_.chunks,&chunk,pdMS_TO_TICKS(10))!=pdTRUE){if(j_.cancel.load()||uint32_t(millis()-began)>=j_.timeout)return at;}
   at+=chunk.length;j_.received+=chunk.length;}return n;}if(memchr(p,0,n)){binary=true;return 0;}memcpy(j_.response+j_.received,p,n);j_.received+=n;j_.response[j_.received]=0;return n;}
};
static void networkTask(void*context){
 auto j=static_cast<NetworkJob*>(context);
 {
  HTTPClient client;NetworkClient plain;NetworkClientSecure secure;
  bool tls=!strncmp(j->url,"https://",8);if(tls)secure.setCACert(j->ca);
  NetworkClient&transport=tls?static_cast<NetworkClient&>(secure):plain;
  client.setTimeout(j->timeout);client.setConnectTimeout(j->timeout);client.setReuse(false);const char*responseHeaders[]={"Content-Type"};client.collectHeaders(responseHeaders,1);
  if(j->cancel.load())copy(j->error,sizeof j->error,"cancelled");
  else if(!client.begin(transport,j->url))copy(j->error,sizeof(j->error),"request_begin_failed");
  else {
    JsonDocument headers;deserializeJson(headers,j->headers);for(auto p:headers.as<JsonObjectConst>())client.addHeader(p.key().c_str(),p.value().as<const char*>());
    j->status=client.sendRequest(j->method,(uint8_t*)j->body,strlen(j->body));
    if(j->status<=0)copy(j->error,sizeof(j->error),"network_request_failed");
    else if(strcmp(j->method,"HEAD")){ResponseSink sink(*j);int result=client.writeToStream(&sink);if(sink.binary)copy(j->error,sizeof(j->error),"binary_response_requires_stream");else if(sink.overflow)copy(j->error,sizeof(j->error),"response_limit");else if(result<0)copy(j->error,sizeof(j->error),"response_read_failed");}
    client.end();
  }
 }
 if(j->cancel.load())copy(j->error,sizeof j->error,"cancelled");
 j->done=true;j->busy=false;vTaskDelete(nullptr);
}
static void describeNetworkResult(JsonObject o){
 auto result=httpOutcome(networkJob->status,networkJob->error);
 o["transport_ok"]=result.transportOk;o["http_ok"]=result.httpOk;o["ok"]=result.ok;o["error"]=result.error;
}
static void pollNetwork(){
 if(networkJob&&!networkJob->busy)core.resources().releaseUnused(networkJob->reservation);
 if(networkJob&&networkJob->orphan&&!networkJob->busy){delete networkJob;networkJob=nullptr;networkNotified=false;return;}
 if(networkJob&&networkJob->stream&&uxQueueMessagesWaiting(networkJob->chunks)&&!networkJob->readableNotified){networkJob->readableNotified=true;JsonDocument d;d["job_id"]=networkJob->id;d["owner"]=networkJob->owner;emit("/net/http","readable",d.as<JsonObjectConst>(),networkJob->owner);}
 if(!networkJob||networkJob->busy||!networkJob->done||networkNotified)return;networkNotified=true;
 auto result=httpOutcome(networkJob->status,networkJob->error);
 auto&j=jobs[networkJob->id%8];if(j.id==networkJob->id){j.complete=true;j.ok=result.ok;copy(j.state,sizeof(j.state),result.ok?"completed":"failed");copy(j.error,sizeof(j.error),result.error);}
 JsonDocument d;auto o=d.to<JsonObject>();o["job_id"]=networkJob->id;o["owner"]=networkJob->owner;o["http_status"]=networkJob->status;o["bytes"]=networkJob->received;
 describeNetworkResult(o);emit("/net/http",result.ok?"completed":"failed",o,networkJob->owner);
}
static const char*networkHandler(const char*m,JsonObjectConst a,JsonObject out){
 const bool request=!strcmp(m,"request"),read=!strcmp(m,"read");
 if(!request&&!read&&strcmp(m,"receive")&&strcmp(m,"cancel")&&strcmp(m,"release"))return "unsupported_method";
 const char*allowed=request?"url,method,body,headers,stream,response_limit,timeout_ms,ca_pem":"job_id";
 for(auto f:a)if(!Policies::choice(allowed,f.key().c_str()))return "unknown_http_argument";
 if(a.containsKey("job_id")&&(!a["job_id"].is<uint32_t>()||!a["job_id"].as<uint32_t>()))return "invalid_job_id";
 if(!request&&!read&&!a.containsKey("job_id"))return "job_id_required";
 if(!read)pollNetwork();
 if(a.containsKey("job_id")&&(!networkJob||networkJob->id!=a["job_id"].as<uint32_t>()))return "network_job_not_found";
 if(networkJob&&(!read||a.containsKey("job_id"))&&core.callerOwner()&&networkJob->owner!=core.callerOwner())return "network_owned_by_other";
 if(!strcmp(m,"cancel")){if(!networkJob->busy)return "network_job_finished";networkJob->cancel=true;out["status"]="cancel_requested";out["job_id"]=networkJob->id;return nullptr;}
 if(!strcmp(m,"release")){if(networkJob->busy)return "network_job_busy";delete networkJob;networkJob=nullptr;networkNotified=false;return nullptr;}
 if(!strcmp(m,"receive")){
   if(!networkJob->stream)return "no_stream_response";
   // Observe producer completion before reading the queue. An empty queue just
   // before a producer's final enqueue must never be reported as EOF.
   bool finished=!networkJob->busy.load();out["job_id"]=networkJob->id;
   NetworkChunk chunk;if(xQueueReceive(networkJob->chunks,&chunk,0)!=pdTRUE){out["available"]=false;out["finished"]=finished;if(finished)describeNetworkResult(out);return nullptr;}
   char data[1025];hex(chunk.data,chunk.length,data);out["available"]=true;out["hex"]=data;out["bytes"]=chunk.length;
   out["finished"]=finished&&!uxQueueMessagesWaiting(networkJob->chunks);networkJob->readableNotified=false;return nullptr;
 }
 if(read){out["active"]=networkActive();out["response_pending"]=networkJob!=nullptr;out["max_inline_response_bytes"]=4096;out["stream_chunk_bytes"]=512;out["https_trust"]="caller_provided_ca";
   if(networkJob){out["job_id"]=networkJob->id;out["stream"]=networkJob->stream;out["owner"]=networkJob->owner;out["cancel_requested"]=networkJob->cancel.load();out["buffered_chunks"]=networkJob->chunks?uxQueueMessagesWaiting(networkJob->chunks):0;
     out["state"]="running";if(!networkJob->busy){out["state"]=httpOutcome(networkJob->status,networkJob->error).ok?"completed":"failed";out["status"]=networkJob->status;describeNetworkResult(out);
       if(!core.callerOwner()||core.callerOwner()==networkJob->owner)out["body"]=networkJob->response;}}
   return nullptr;
 }
 if(networkJob)return networkJob->busy?"network_job_busy":"network_result_pending";
 for(auto key:{"url","method","body","ca_pem"})if(a.containsKey(key)&&(!a[key].is<const char*>()||strlen(a[key].as<const char*>())!=a[key].as<JsonString>().size()))return "http_string_required";
 for(auto key:{"response_limit","timeout_ms"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "http_integer_required";
 if(a.containsKey("stream")&&!a["stream"].is<bool>())return "http_boolean_required";
 if(a.containsKey("headers")&&!a["headers"].is<JsonObjectConst>())return "invalid_headers";
 const char*url=a["url"]|"",*method=a["method"]|"GET";bool tls=!strncmp(url,"https://",8);
 if((strncmp(url,"http://",7)&&!tls)||strlen(url)>=1024)return "invalid_url";
 if(!Policies::choice("GET,POST,PUT,DELETE,HEAD",method))return "unsupported_http_method";
 bool stream=a["stream"]|false;
 unsigned limit=a["response_limit"]|(stream?16777216u:4096u),timeout=a["timeout_ms"]|10000u;
 if(!limit||(!stream&&limit>4096)||!timeout||timeout>60000)return "request_limit";
 const char*ca=a["ca_pem"]|"";if(tls&&!*ca)return "https_ca_required";if(strlen(ca)>8192)return "ca_limit";
 if(!core.resources().canAllocate(tls?65000u:20000u))return "network_memory_budget";
 auto next=new(std::nothrow) NetworkJob;if(!next)return "memory_low";next->reservation=core.resources().acquire(tls?65000u:20000u,core.callerOwner());if(!next->reservation){delete next;return "network_memory_budget";}
 if(!copy(next->url,sizeof(next->url),url)||!copy(next->method,sizeof(next->method),method)||!copy(next->body,sizeof(next->body),a["body"]|"")){delete next;return "request_body_limit";}
 auto headers=a["headers"].as<JsonObjectConst>();if(measureJson(headers)>=sizeof(next->headers)){delete next;return "header_limit";}
 for(auto p:headers)if(!p.value().is<const char*>()||strlen(p.value().as<const char*>())!=p.value().as<JsonString>().size()||strchr(p.key().c_str(),'\r')||strchr(p.key().c_str(),'\n')||strchr(p.value().as<const char*>(),'\r')||strchr(p.value().as<const char*>(),'\n')){delete next;return "invalid_header";}
 if(stream){next->chunks=xQueueCreate(4,sizeof(NetworkChunk));if(!next->chunks){delete next;return "memory_low";}next->stream=true;}
 serializeJson(headers,next->headers,sizeof(next->headers));if(tls){next->ca=strdup(ca);if(!next->ca){delete next;return "memory_low";}}
 // Result storage remains live until an explicit release or application teardown.
 // Queue metadata is conservatively included in the native allocation charge.
 core.resources().used(next->reservation,sizeof(NetworkJob)+(tls?strlen(ca)+1:0)+(stream?4*sizeof(NetworkChunk)+sizeof(StaticQueue_t):0));
 next->limit=limit;next->timeout=timeout;next->id=nextJob;next->owner=core.callerOwner();
 if(xTaskCreate(networkTask,"notes-http",6144,next,1,nullptr)!=pdPASS){delete next;return "task_allocation_failed";}
 networkJob=next;networkNotified=false;++nextJob;rememberJob(next->id);
 out["job_id"]=next->id;out["status"]="accepted";return nullptr;
}
