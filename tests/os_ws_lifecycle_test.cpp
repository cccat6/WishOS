// Exercise the actual WebSocket adapter with a blocking transport in its task.
#include "Core.h"
#include "WireBytes.h"
#include "host/Arduino.h"
#include <functional>
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
struct Queue {size_t size,capacity;std::mutex lock;std::condition_variable ready;std::deque<std::vector<uint8_t>>data;};using QueueHandle_t=Queue*;using StaticQueue_t=Queue;
static void(*emptyReceiveHook)(Queue*)=nullptr;
static unsigned queues=0;static Queue* xQueueCreate(unsigned n,size_t size){++queues;return new Queue{size,n};}
static void vQueueDelete(Queue*q){--queues;delete q;}
static unsigned uxQueueMessagesWaiting(Queue*q){std::lock_guard<std::mutex>g(q->lock);return q->data.size();}
static int xQueueSend(Queue*q,const void*data,unsigned timeout){std::unique_lock<std::mutex>g(q->lock);if(!q->ready.wait_for(g,std::chrono::milliseconds(timeout),[&]{return q->data.size()<q->capacity;}))return 0;auto p=(const uint8_t*)data;q->data.emplace_back(p,p+q->size);q->ready.notify_all();return pdTRUE;}
static int xQueueReceive(Queue*q,void*out,unsigned){std::lock_guard<std::mutex>g(q->lock);if(q->data.empty()){if(emptyReceiveHook)emptyReceiveHook(q);return 0;}memcpy(out,q->data.front().data(),q->size);q->data.pop_front();q->ready.notify_all();return pdTRUE;}

static std::atomic<bool>holdTransport{false},entered{false};static bool failLaunch=false;static std::vector<std::thread>tasks;
static int xTaskCreate(void(*fn)(void*),const char*,unsigned,void*p,unsigned,void*){if(failLaunch)return 0;tasks.emplace_back([=]{fn(p);});return pdPASS;}
static void vTaskDelete(void*){}static void vTaskDelay(unsigned n){std::this_thread::sleep_for(std::chrono::milliseconds(n));}
enum WStype_t{WStype_CONNECTED,WStype_DISCONNECTED,WStype_FRAGMENT_TEXT_START,WStype_FRAGMENT_BIN_START,WStype_FRAGMENT,WStype_FRAGMENT_FIN,WStype_TEXT,WStype_BIN};
struct WebSocketsClient{
 std::function<void(WStype_t,uint8_t*,size_t)>callback;const char*headers=nullptr;bool connected=false;unsigned loops=0;
 void onEvent(std::function<void(WStype_t,uint8_t*,size_t)>fn){callback=fn;}void setReconnectInterval(unsigned){}void setExtraHeaders(const char*h){headers=h;}
 void begin(const char*,unsigned,const char*,const char*){}void beginSslWithCA(const char*,unsigned,const char*,const char*,const char*){}
 void loop(){entered=true;while(holdTransport.load())std::this_thread::yield();if(!connected){assert(headers&&strstr(headers,"X-Test: retained"));connected=true;callback(WStype_CONNECTED,nullptr,0);}++loops;}
 bool sendBIN(uint8_t*p,size_t n){callback(WStype_BIN,p,n);return true;}bool sendTXT(uint8_t*p,size_t n){callback(WStype_TEXT,p,n);return true;}
 void disconnect(){if(connected){connected=false;callback(WStype_DISCONNECTED,nullptr,0);}}
};
static Core core;static void emit(const char*p,const char*e,JsonObjectConst d,uint32_t owner=0){core.emit(p,e,d,owner);}
#include "drivers/esp32-services/WebSocketService.h"
static void join(){for(auto&t:tasks)t.join();tasks.clear();pollWebSocket();}
int main(){JsonDocument a,out,empty,send,completion;a["host"]="test";a["headers"]["X-Test"]="retained";core.add({"/net/ws","read,connect,send,receive,close","",wsHandler,wsAsyncDescription});auto owner=core.acquireOwner(),other=core.acquireOwner();
 // The call returns while the native connect remains blocked. Closing keeps the
 // state alive until that task has left the transport; another app cannot steal it.
 holdTransport=true;entered=false;assert(!core.request("/net/ws","connect",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));uint32_t receipt=out["completion_id"];
 while(!entered.load())std::this_thread::yield();assert(!strcmp(core.invoke("/net/ws","close",empty.to<JsonObject>(),out.to<JsonObject>(),other),"ws_owned_by_other"));
 assert(!core.invoke("/net/ws","close",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));pollWebSocket();assert(wsSession&&!wsSession->done);
 holdTransport=false;join();assert(!wsSession&&queues==0);assert(!core.completions().read(receipt,owner,completion.to<JsonObject>()));assert(completion["ready"]==true);assert(!core.completions().release(receipt,owner));assert(core.resources().promised()==0);
 // Queue payloads survive the call, get an individual terminal receipt and can
 // be received without requiring the worker to call application code.
 assert(!core.request("/net/ws","connect",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));receipt=out["completion_id"];
 auto deadline=millis()+2000;while(!wsSession->connected){assert(millis()<deadline);std::this_thread::yield();}assert(!core.completions().release(receipt,owner));
 send["text"]="hello";assert(!core.request("/net/ws","send",send.as<JsonObjectConst>(),out.to<JsonObject>(),owner));receipt=out["completion_id"];send.clear();
 while(!core.completions().ready(owner)){assert(millis()<deadline);std::this_thread::yield();}assert(!core.completions().read(receipt,owner,completion.to<JsonObject>()));assert(completion["event"]=="sent");
 assert(!core.invoke("/net/ws","receive",empty.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(out["hex"]=="68656c6c6f");assert(!core.completions().release(receipt,owner));
 stopWebSocket();join();assert(!wsSession&&queues==0&&core.resources().promised()==0);
 // Failed task launch and malformed admission leave no transport or reservation.
 failLaunch=true;assert(!strcmp(core.request("/net/ws","connect",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner),"task_allocation_failed"));assert(!wsSession&&queues==0&&core.resources().promised()==0);failLaunch=false;
 a["port"]=0;assert(core.invoke("/net/ws","connect",a.as<JsonObjectConst>(),out.to<JsonObject>(),owner));assert(!wsSession&&core.resources().promised()==0);
 std::cout<<"WebSocket blocked connect, retained headers, queued sends, result correlation, receive, deferred close, owner isolation and failed launch passed\n";
}
