#include "audio/RecordingBuffer.h"
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
 std::cout<<"Independent compressed recording sessions, retryable reads, acknowledgements, loss and RAM reclamation passed\n";
}
