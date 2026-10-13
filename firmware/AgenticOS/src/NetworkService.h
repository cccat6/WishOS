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
static void networkTask(void*){
 auto j=networkJob;
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
    else if(strcmp(j->method,"HEAD")){ResponseSink sink(*j);int result=client.writeToStream(&sink);if(sink.binary)copy(j->error,sizeof(j->error),"binary_response_requires_file_transport");else if(sink.overflow)copy(j->error,sizeof(j->error),"response_limit");else if(result<0)copy(j->error,sizeof(j->error),"response_read_failed");}
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
 if(networkJob&&!networkJob->busy&&networkJob->reservation){core.resources().release(networkJob->reservation);networkJob->reservation=0;}
 if(networkJob&&networkJob->orphan&&!networkJob->busy){delete networkJob;networkJob=nullptr;networkNotified=false;return;}
 if(networkJob&&networkJob->stream&&uxQueueMessagesWaiting(networkJob->chunks)&&!networkJob->readableNotified){networkJob->readableNotified=true;JsonDocument d;d["job_id"]=networkJob->id;d["owner"]=networkJob->owner;emit("/net/http","readable",d.as<JsonObjectConst>(),networkJob->owner);}
 if(!networkJob||networkJob->busy||!networkJob->done||networkNotified)return;networkNotified=true;
 auto result=httpOutcome(networkJob->status,networkJob->error);
 auto&j=jobs[networkJob->id%8];if(j.id==networkJob->id){j.complete=true;j.ok=result.ok;copy(j.state,sizeof(j.state),result.ok?"completed":"failed");copy(j.error,sizeof(j.error),result.error);}
 JsonDocument d;auto o=d.to<JsonObject>();o["job_id"]=networkJob->id;o["owner"]=networkJob->owner;o["http_status"]=networkJob->status;o["bytes"]=networkJob->received;
 describeNetworkResult(o);emit("/net/http",result.ok?"completed":"failed",o,networkJob->owner);
}
static const char*networkHandler(const char*m,JsonObjectConst a,JsonObject out){
 pollNetwork();
 if(networkJob&&strcmp(m,"read")&&core.callerOwner()&&networkJob->owner!=core.callerOwner())return "network_owned_by_other";
 if(!strcmp(m,"cancel")){if(!networkJob)return "network_job_not_found";networkJob->cancel=true;out["status"]="cancel_requested";out["job_id"]=networkJob->id;return nullptr;}
 if(!strcmp(m,"release")){if(networkJob&&networkJob->busy)return "network_job_busy";delete networkJob;networkJob=nullptr;networkNotified=false;return nullptr;}
 if(!strcmp(m,"receive")){
   if(!networkJob||!networkJob->stream)return "no_stream_response";
   NetworkChunk chunk;if(xQueueReceive(networkJob->chunks,&chunk,0)!=pdTRUE){out["available"]=false;out["finished"]=!networkJob->busy;return nullptr;}
   char data[1025];hex(chunk.data,chunk.length,data);out["available"]=true;out["hex"]=data;out["bytes"]=chunk.length;out["job_id"]=networkJob->id;networkJob->readableNotified=false;return nullptr;
 }
 if(!strcmp(m,"read")){out["active"]=networkActive();out["max_inline_response_bytes"]=4096;out["stream_chunk_bytes"]=512;out["https_trust"]="caller_provided_ca";if(networkJob){out["job_id"]=networkJob->id;out["stream"]=networkJob->stream;out["owner"]=networkJob->owner;out["cancel_requested"]=networkJob->cancel.load();out["buffered_chunks"]=networkJob->chunks?uxQueueMessagesWaiting(networkJob->chunks):0;if(!networkJob->busy){out["status"]=networkJob->status;describeNetworkResult(out);out["body"]=networkJob->response;}}return nullptr;}
 if(strcmp(m,"request"))return "unsupported_method";if(networkActive())return "network_job_busy";if(networkJob&&networkJob->chunks&&uxQueueMessagesWaiting(networkJob->chunks))return "response_chunks_pending";
 const char*url=a["url"]|"",*method=a["method"]|"GET";bool tls=!strncmp(url,"https://",8);
 if((strncmp(url,"http://",7)&&!tls)||strlen(url)>=1024)return "invalid_url";
 if(!Policies::choice("GET,POST,PUT,DELETE,HEAD",method))return "unsupported_http_method";
 bool stream=a["stream"]|false;
 unsigned limit=a["response_limit"]|(stream?16777216u:4096u),timeout=a["timeout_ms"]|10000u;
 if(!limit||(!stream&&limit>4096)||!timeout||timeout>60000)return "request_limit";
 const char*ca=a["ca_pem"]|"";if(tls&&!*ca)return "https_ca_required";if(strlen(ca)>8192)return "ca_limit";
 if(!core.resources().canAllocate(tls?65000u:20000u))return "network_memory_budget";
 auto next=new(std::nothrow) NetworkJob;if(!next)return "memory_low";next->reservation=core.resources().acquire(tls?65000u:20000u,core.callerOwner());if(!next->reservation){delete next;return "network_memory_budget";}core.resources().used(next->reservation,sizeof(NetworkJob));
 if(!copy(next->url,sizeof(next->url),url)||!copy(next->method,sizeof(next->method),method)||!copy(next->body,sizeof(next->body),a["body"]|"")){delete next;return "request_body_limit";}
 auto headers=a["headers"].as<JsonObjectConst>();if(measureJson(headers)>=sizeof(next->headers)){delete next;return "header_limit";}
 for(auto p:headers)if(!p.value().is<const char*>()||strchr(p.key().c_str(),'\r')||strchr(p.key().c_str(),'\n')||strchr(p.value().as<const char*>(),'\r')||strchr(p.value().as<const char*>(),'\n')){delete next;return "invalid_header";}
 if(stream){next->chunks=xQueueCreate(4,sizeof(NetworkChunk));if(!next->chunks){delete next;return "memory_low";}next->stream=true;}
 serializeJson(headers,next->headers,sizeof(next->headers));if(tls){next->ca=strdup(ca);if(!next->ca){delete next;return "memory_low";}}
 pollNetwork(); // Completion may arrive during validation; retain its event before replacing the job.
 delete networkJob;networkJob=next;networkNotified=false;next->limit=limit;next->timeout=timeout;next->id=nextJob++;next->owner=core.callerOwner();rememberJob(next->id);
 if(xTaskCreate(networkTask,"notes-http",6144,nullptr,1,nullptr)!=pdPASS){next->busy=false;next->done=true;copy(next->error,sizeof(next->error),"task_allocation_failed");return "task_allocation_failed";}
 out["job_id"]=next->id;out["status"]="accepted";return nullptr;
}
