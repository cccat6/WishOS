#include "../firmware/AgenticOS/src/Core.h"
#include <assert.h>
#include <iostream>
using namespace notesos;
static int calls = 0;
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
int main() {
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
  std::cout << "OS registry/rules/predicates/cooldown/cursor gaps/timer coalescing passed\n";
}
