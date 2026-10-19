// This public header is the first include: it declares its own synchronization
// dependencies without depending on Core or a device's native include order.
#include "CompletionStore.h"
#include <thread>
#include <cassert>
#include <iostream>
using namespace notesos;
int main(){
 ResourceBudget memory;
 {CompletionStore<2> store(memory);JsonDocument spec,reply,out;spec["source"]="/native";spec["events"].to<JsonArray>().add("done");spec["match"]["job"]="id";
  uint32_t id=0;assert(!store.reserve(7,spec.as<JsonObjectConst>(),id,0));
  // Native completion may precede the synchronous request's correlation reply.
  std::thread producer([&](){for(unsigned job:{41u,42u}){JsonDocument data;data["job"]=job;data["value"]=job+1;store.record("/native","done",data.as<JsonObjectConst>(),7,100+job,0);}});
  producer.join();assert(!store.ready(7));reply["id"]=42;assert(!store.bind(id,spec.as<JsonObjectConst>(),reply.as<JsonObjectConst>(),200));assert(store.ready(7));assert(!store.read(id,7,out.to<JsonObject>()));assert(out["ready"]==true&&out["data"]["job"]==42&&out["data"]["value"]==43);
  assert(store.release(id,8));assert(!store.release(id,7));assert(!store.ready(7));
 }
 assert(memory.promised()==0);std::cout<<"Standalone completion header and early native correlation/release passed\n";
}
