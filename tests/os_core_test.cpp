#include "../firmware/WishOS/src/Core.h"
#include <assert.h>
#include <iostream>
using namespace notesos;
static int calls = 0;
static Core*editingCore;static uint32_t replacedRule;static unsigned loadedRules=0;
static const char*replaceDuringEvent(const char*,JsonObjectConst,JsonObject){
  if(!loadedRules++){
    assert(editingCore->removeRule(replacedRule));
    JsonDocument rule,out;rule["source"]="/button";rule["event"]="edit";rule["target"]="/output";rule["method"]="write";rule["cooldown_ms"]=0;
    assert(!editingCore->setRule(rule.as<JsonObjectConst>(),out.to<JsonObject>()));
  }
  return nullptr;
}
static const char *fake(const char *, JsonObjectConst a, JsonObject o) {
  calls++;
  o["value"] = a["value"];
  return nullptr;
}
static JsonDocument json(const char *s) {
  JsonDocument d;
  assert(!deserializeJson(d, s));
  return d;
}
static uint64_t nativeNow=0;
static uint64_t nativeClock(){return nativeNow;}
int main() {
  static unsigned journalFreed=0;
  {Core journal;JsonDocument payload,before,after;payload["value"]=42;
   for(unsigned i=0;i<3;++i)assert(journal.emit("/input","sample",payload.as<JsonObjectConst>(),7,100+i));
   journal.events(0,8,before.to<JsonObject>());std::string snapshot;serializeJson(before,snapshot);
   assert(!journal.eventStorage([](size_t)->void*{return nullptr;},free));journal.events(0,8,after.to<JsonObject>());std::string rejected;serializeJson(after,rejected);assert(rejected==snapshot);
   assert(journal.eventStorage([](size_t n)->void*{return malloc(n);},[](void*p){++journalFreed;free(p);}));journal.events(0,8,after.to<JsonObject>());std::string moved;serializeJson(after,moved);assert(moved==snapshot);
   assert(journal.emit("/input","sample",payload.as<JsonObjectConst>(),8,200));journal.events(3,8,after.to<JsonObject>());assert(after["events"][0]["sequence"]==4&&after["events"][0]["target_owner"]==8);
  }assert(journalFreed==1);
  Core timed;timed.clock(1);timed.clockSource(nativeClock);nativeNow=500;assert(timed.now()==500);timed.clock(2);assert(timed.now()==500);timed.clockSource(nullptr);assert(timed.now()==2);
  Core c;
  assert(c.add({"/button", "read", "", fake}));
  assert(c.add({"/output", "write", "", fake}));
  assert(!c.add({"/button", "read", "", fake}));
  JsonDocument args, result;
  assert(!strcmp(c.invoke("/missing", "read", args.as<JsonObjectConst>(), result.to<JsonObject>()),
                 "not_found"));
  assert(!strcmp(c.invoke("/output", "read", args.as<JsonObjectConst>(), result.to<JsonObject>()),
                 "unsupported_method"));
  auto rule = json(
      R"({"source":"/button","event":"release","where":{"button":"B"},"target":"/output","method":"write","args":{"value":42},"cooldown_ms":100})");
  assert(!c.setRule(rule.as<JsonObjectConst>(), result.to<JsonObject>()));
  auto invalid = json(R"({"source":"/button","event":"press","target":"/output","method":"read"})");
  assert(c.setRule(invalid.as<JsonObjectConst>(), result.to<JsonObject>()));
  uint32_t cursor = 0;
  c.clock(10);
  auto data = json(R"({"button":"A"})");
  c.emit("/button", "release", data.as<JsonObjectConst>());
  c.dispatch(cursor);
  assert(calls == 0);
  data = json(R"({"button":"B"})");
  c.emit("/button", "release", data.as<JsonObjectConst>());
  c.dispatch(cursor);
  assert(calls == 1);
  c.clock(30);
  c.emit("/button", "release", data.as<JsonObjectConst>());
  c.dispatch(cursor);
  assert(calls == 1);
  c.clock(110);
  c.emit("/button", "release", data.as<JsonObjectConst>());
  c.dispatch(cursor);
  assert(calls == 2);
  c.dispatch(cursor);
  assert(calls == 2);
  for (int i = 0; i < 50; i++)
    c.emit("/button", "press", data.as<JsonObjectConst>());
  c.events(0, 8, result.to<JsonObject>());
  assert(result["gap"].as<bool>());
  assert(result["events"].size() == 8);
  auto next = result["cursor"].as<uint32_t>();
  c.events(next, 8, result.to<JsonObject>());
  assert(result["events"][0]["sequence"].as<uint32_t>() == next + 1);
  auto timer = json(R"({"id":1,"delay_ms":10,"period_ms":20})");
  c.clock(200);
  assert(!c.timer(timer.as<JsonObjectConst>(), result.to<JsonObject>()));
  uint32_t before = c.sequence();
  c.clock(290);
  c.tick();
  assert(c.sequence() == before + 1);
  assert(c.nextTimer() == 310);
  c.tick();
  assert(c.sequence() == before + 1);
  assert(c.removeTimer(1));
  assert(!c.nextTimer());
  auto threshold = json(
      R"({"source":"/button","event":"sample","where":{"mv":{"lt":3600}},"target":"/output","method":"write","args":{"value":7}})");
  assert(!c.setRule(threshold.as<JsonObjectConst>(), result.to<JsonObject>()));
  auto reading = json(R"({"mv":3500})");
  int n = calls;
  c.emit("/button", "sample", reading.as<JsonObjectConst>());
  c.dispatch(cursor);
  assert(calls == n + 1);
  auto bad = json(
      R"({"source":"/button","event":"sample","where":{"mv":{"wat":3600}},"target":"/output","method":"write"})");
  assert(c.setRule(bad.as<JsonObjectConst>(), result.to<JsonObject>()));
  auto badTimer = json(R"({"id":2,"delay_ms":10,"period_ms":"x"})");
  assert(c.timer(badTimer.as<JsonObjectConst>(), result.to<JsonObject>()));
  // Loading rules from a rule action cannot retroactively handle the same input.
  Core editing;editingCore=&editing;editing.add({"/button","read","",fake});editing.add({"/output","write","",fake});editing.add({"/loader","load","",replaceDuringEvent});
  auto load=json(R"({"source":"/button","event":"edit","target":"/loader","method":"load","cooldown_ms":0})");
  assert(!editing.setRule(load.as<JsonObjectConst>(),result.to<JsonObject>()));
  load["target"]="/output";load["method"]="write";assert(!editing.setRule(load.as<JsonObjectConst>(),result.to<JsonObject>()));replacedRule=result["id"];
  uint32_t editCursor=0;int previousCalls=calls;editing.emit("/button","edit",args.as<JsonObjectConst>());editing.dispatch(editCursor);assert(calls==previousCalls);
  editing.emit("/button","edit",args.as<JsonObjectConst>());editing.dispatch(editCursor);assert(calls==previousCalls+1);
  // A replacement group cannot overwrite an unrelated rule by supplying its ID.
  JsonDocument definitions;load["id"]=1;definitions.to<JsonArray>().add(load);JsonDocument ids;
  assert(editing.replaceRules(definitions.as<JsonArrayConst>(),nullptr,0,ids.to<JsonArray>()));
  editing.emit("/button","edit",args.as<JsonObjectConst>());editing.dispatch(editCursor);assert(loadedRules==3);
  // A queued result for the previous instance does not invoke a replacement's
  // rule, even if both apps use the same source and event names.
  Core routing;editingCore=&routing;routing.add({"/button","read","",fake});routing.add({"/output","write","",fake});
  routing.add({"/rules","write","",[](const char*,JsonObjectConst a,JsonObject o){return editingCore->setRule(a,o);}});
  load.remove("id");uint32_t owner=routing.acquireOwner(),oldOwner=routing.acquireOwner();
  assert(!routing.invoke("/rules","write",load.as<JsonObjectConst>(),result.to<JsonObject>(),owner));
  uint32_t routeCursor=0;previousCalls=calls;routing.emit("/button","edit",args.as<JsonObjectConst>(),oldOwner);routing.dispatch(routeCursor);assert(calls==previousCalls);
  routing.emit("/button","edit",args.as<JsonObjectConst>(),owner);routing.dispatch(routeCursor);assert(calls==previousCalls+1);
  // Rules installed while inputs wait behind a display gate start with future
  // events, not with the backlog produced for the preceding rule set.
  Core queued;queued.add({"/button","read","",fake});queued.add({"/output","write","",fake});
  static bool hold=true;queued.setEventGate([](const Event&){return hold?1:0;});
  assert(!queued.setRule(load.as<JsonObjectConst>(),result.to<JsonObject>()));uint32_t pendingCursor=0;
  queued.emit("/button","edit",args.as<JsonObjectConst>());queued.dispatch(pendingCursor);assert(queued.deferredCount()==1);
  assert(!queued.setRule(load.as<JsonObjectConst>(),result.to<JsonObject>()));previousCalls=calls;
  hold=false;queued.dispatch(pendingCursor);assert(calls==previousCalls+1); // only the old rule
  queued.emit("/button","edit",args.as<JsonObjectConst>());queued.dispatch(pendingCursor);assert(calls==previousCalls+3);
  std::cout << "OS registry/rules/predicates/cooldown/cursor gaps/timer coalescing passed\n";
}
