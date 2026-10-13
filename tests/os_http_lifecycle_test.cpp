// Run the actual Arduino HTTP service lifecycle with a controlled transport and queue backend.
#include "Core.h"
#include "DriverRequests.h"
#include "Policy.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>
#include <algorithm>
using namespace notesos;
using TickType_t=unsigned;constexpr int pdTRUE=1,pdPASS=1;
static uint32_t millis(){static auto start=std::chrono::steady_clock::now();return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();}
static unsigned pdMS_TO_TICKS(unsigned n){return n;}
struct Queue {size_t size,capacity;std::mutex lock;std::condition_variable ready;std::deque<std::vector<uint8_t>>data;};using QueueHandle_t=Queue*;
static unsigned queues=0;static Queue* xQueueCreate(unsigned n,size_t size){++queues;return new Queue{size,n};}
static void vQueueDelete(Queue*q){--queues;delete q;}
static unsigned uxQueueMessagesWaiting(Queue*q){std::lock_guard<std::mutex>g(q->lock);return q->data.size();}
static int xQueueSend(Queue*q,const void*data,unsigned timeout){std::unique_lock<std::mutex>g(q->lock);if(!q->ready.wait_for(g,std::chrono::milliseconds(timeout),[&]{return q->data.size()<q->capacity;}))return 0;auto p=(const uint8_t*)data;q->data.emplace_back(p,p+q->size);q->ready.notify_all();return pdTRUE;}
static int xQueueReceive(Queue*q,void*out,unsigned){std::lock_guard<std::mutex>g(q->lock);if(q->data.empty())return 0;memcpy(out,q->data.front().data(),q->size);q->data.pop_front();q->ready.notify_all();return pdTRUE;}
struct Stream {virtual ~Stream()=default;virtual int available()=0;virtual int read()=0;virtual int peek()=0;virtual void flush()=0;virtual size_t write(uint8_t)=0;virtual size_t write(const uint8_t*,size_t)=0;};
struct NetworkClient{virtual ~NetworkClient()=default;};struct NetworkClientSecure:NetworkClient{void setCACert(const char*){}};
static std::atomic<bool> holdTransport{false},entered{false};static bool failLaunch=false;
struct HTTPClient{
 void setTimeout(unsigned){}void setConnectTimeout(unsigned){}void setReuse(bool){}void collectHeaders(const char**,unsigned){}bool begin(NetworkClient&,const char*){return true;}
 void addHeader(const char*,const char*){}int sendRequest(const char*,uint8_t*,size_t){entered=true;while(holdTransport.load())std::this_thread::yield();return 200;}
 int writeToStream(Stream*s){uint8_t bytes[512];memset(bytes,'a',sizeof bytes);for(unsigned i=0;i<16;++i)if(s->write(bytes,sizeof bytes)!=sizeof bytes)return -1;return 8192;}void end(){}
};
static std::vector<std::thread> tasks;
static int xTaskCreate(void(*fn)(void*),const char*,unsigned,void*p,unsigned,void*){if(failLaunch)return 0;tasks.emplace_back([=]{fn(p);});return pdPASS;}static void vTaskDelete(void*){}
struct{size_t getFreeHeap(){return 200000;}}ESP;
static Core core;static uint32_t nextJob=1;
struct Receipt{uint32_t id=0;bool complete=false,ok=false;char state[48]{},error[80]{};};static Receipt jobs[8];static void rememberJob(uint32_t id){jobs[id%8]={};jobs[id%8].id=id;}
static void emit(const char*p,const char*e,JsonObjectConst d,uint32_t targetOwner=0){core.emit(p,e,d,targetOwner);}
static void hex(const uint8_t*p,size_t n,char*out){for(size_t i=0;i<n;++i)sprintf(out+i*2,"%02x",p[i]);out[n*2]=0;}
#include "NetworkService.h"
static void join(){for(auto&t:tasks)t.join();tasks.clear();}
int main(){JsonDocument a,out,empty;a["url"]="http://test/";a["stream"]=true;a["response_limit"]=8192;a["timeout_ms"]=2000;
 core.add({"/net/http","read,request,receive,cancel,release","",networkHandler});auto owner=core.acquireOwner(),other=core.acquireOwner();
 assert(!core.invoke("/net/http","request",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));
 // Fill the queue without reading: producer blocks but consumer loop retains control.
 auto deadline=millis()+2000;while(uxQueueMessagesWaiting(networkJob->chunks)<4){assert(millis()<deadline);std::this_thread::yield();}
 assert(!strcmp(core.invoke("/net/http","cancel",empty.as<JsonObjectConst>(),out.to<JsonObject>(),other),"network_owned_by_other"));
 assert(!core.invoke("/net/http","cancel",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));join();pollNetwork();assert(!networkJob->busy&&!strcmp(networkJob->error,"cancelled"));assert(!core.invoke("/net/http","release",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(!networkJob&&queues==0);
 // Cancellation while the transport is still blocked never frees task-owned data.
 holdTransport=true;entered=false;assert(!core.invoke("/net/http","request",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));while(!entered.load())std::this_thread::yield();assert(!core.invoke("/net/http","cancel",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(!strcmp(core.invoke("/net/http","release",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"network_job_busy"));networkJob->orphan=true;pollNetwork();assert(networkJob);holdTransport=false;join();pollNetwork();assert(!networkJob&&queues==0);
 // Normal stream delivery still completes after the cancelled and orphaned cases.
 assert(!core.invoke("/net/http","request",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));size_t bytes=0;deadline=millis()+3000;
 while(networkActive()){assert(millis()<deadline);assert(!core.invoke("/net/http","receive",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));if(out["available"]==true)bytes+=out["bytes"].as<unsigned>();std::this_thread::yield();}join();pollNetwork();assert(bytes==8192&&networkJob->error[0]==0);assert(!core.invoke("/net/http","release",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(queues==0);
 failLaunch=true;assert(!strcmp(core.invoke("/net/http","request",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"task_allocation_failed"));assert(!networkJob->busy);assert(!core.invoke("/net/http","release",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(queues==0);
 std::cout<<"HTTP stream backpressure, cancellation, late task completion, orphan cleanup, task-allocation failure and subsequent recovery passed\n";
}
