#include "audio/RecordingBuffer.h"
#include "RpcEnvelope.h"
#include <cassert>
#include <iostream>
using namespace notesos;
using Buffer=audio::RecordingBuffer<>;
static Buffer::Chunk block(unsigned n){Buffer::Chunk b;b.sequence=n;b.samples=160;b.bytes=91;b.sampleStart=n*160;b.data[0]=0x42;b.data[1]=n;b.data[9]=160;return b;}
int main(){ResourceBudget memory;Buffer recordings(memory);JsonDocument a,o;
 assert(!recordings.begin("first",7,16000,2));recordings.push(block(0));recordings.push(block(1));
 a["recording_id"]="first";assert(!recordings.invoke("read_chunk",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));std::string first;serializeJson(o,first);assert(o["chunks"].size()==2&&o["offered_sequence"]==2);
 assert(!recordings.invoke("read_chunk",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));std::string retried;serializeJson(o,retried);assert(first==retried);
 assert(recordings.push(block(2))==160);recordings.finish();assert(!recordings.begin("second",7,48000,1));recordings.push(block(0));
 assert(!recordings.invoke("read_chunk",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));assert(o["recording_id"]=="first"&&o["recording"]==false&&o["dropped_samples"]=="160"&&o["chunks"][0]["sequence"]==1);
 a["next_sequence"]=4;assert(recordings.invoke("ack",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));a["next_sequence"]=3;assert(!recordings.invoke("ack",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));assert(o["buffered_chunks"]==0&&o["buffer_blocks"]==0);
 a.remove("next_sequence");assert(recordings.invoke("read",a.as<JsonObjectConst>(),o.to<JsonObject>(),8));assert(!recordings.invoke("release",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));
 recordings.finish("capture_failed");a["recording_id"]="second";assert(!recordings.invoke("read",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));assert(o["sample_rate"]==48000&&o["capture_failed"]==true);
 assert(!recordings.invoke("release",a.as<JsonObjectConst>(),o.to<JsonObject>(),7));assert(memory.promised()==0);
 JsonDocument limit;limit["memory_limit_bytes"]=100;assert(!memory.configure(limit.as<JsonObjectConst>()));assert(recordings.begin("no-memory",7,16000,2));assert(memory.promised()==0);
 // The requested normal batch must survive the actual RPC envelope. Large
 // metadata shrinks the batch, without offering or acknowledging unseen data.
 {ResourceBudget budget;
  for(bool verbose:{false,true}){
   Buffer stream(budget);
   assert(!stream.begin("batch",7,16000,20));for(unsigned i=0;i<20;++i)stream.push(block(i));
   if(verbose){std::string file(250,'"'),error(70,'"');stream.storage("batch",file.c_str(),false,false,error.c_str());stream.finish(error.c_str());}
   // Bind after building the session; owner zero is the system collector.
   Core rpc;Device device{"/dev/mic","read_chunk","",nullptr};device.context=&stream;device.contextHandle=[](void*p,const char*m,JsonObjectConst a,JsonObject o){return static_cast<Buffer*>(p)->invoke(m,a,o,0);};rpc.add(device);
   JsonDocument request,reply;request["id"]="collector:1";request["path"]="/dev/mic";request["method"]="read_chunk";request["args"]["recording_id"]="batch";request["args"]["limit"]=16;
   invokeEnvelope(rpc,request.as<JsonObjectConst>(),reply,"boot-fixture",1234);
   assert(reply["ok"]==true&&measureJson(reply)<=MAX_WIRE);auto chunks=reply["result"]["chunks"].as<JsonArray>();unsigned received=chunks.size();assert(received>0&&(verbose?received<16:received==16));assert(reply["result"]["offered_sequence"]==received);
   request["args"]["after_sequence"]=received;invokeEnvelope(rpc,request.as<JsonObjectConst>(),reply,"boot-fixture",1235);
   assert(reply["ok"]==true&&measureJson(reply)<=MAX_WIRE&&reply["result"]["chunks"][0]["sequence"]==received&&reply["result"]["acknowledged_sequence"]==received);
  }
 }
 std::cout<<"Independent compressed recording sessions, retryable reads, acknowledgements, loss and RAM reclamation passed\n";
}
