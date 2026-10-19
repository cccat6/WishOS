#include "drivers/sd-spi-volume/SdClockPlan.h"
#include "fixtures/composition/Defaults.h"
#include <cassert>
#include <cstring>
using namespace notesos;
int main() {
  uint32_t requests[256]{}, actual[256]{};
  auto c6 = [](uint32_t hz) { return hz == 25000000 ? 20000000 : hz; };
  assert(!sdClockExact(25000000,c6));
  assert(sdClockExact(20000000,c6));
  size_t n = sdClockCandidates(80000000,25000000, c6, requests, actual, 256);
  assert(n == 77 && requests[0] == 20000000 && actual[0] == 20000000);
  assert(actual[1] == 16000000 && actual[n - 1] == 1000000);
  n = sdClockCandidates(80000000,8000000, c6, requests, actual, 256);
  assert(n == 71 && actual[0] == 8000000 && actual[n - 1] == 1000000);
  n = sdClockCandidates(80000000,999999, c6, requests, actual, 256);
  assert(n == 0);
  Policies policy(SETTINGS,sizeof(SETTINGS)/sizeof(SETTINGS[0]));
  JsonDocument input,response;
  deserializeJson(input,R"({"scope":"system","changes":[{"resource":"/sys/bus","settings":{"sd_spi_mode":"fixed","sd_spi_hz":40000000,"display_spi_hz":16000000}}]})");
  assert(!policy.apply(input.as<JsonObjectConst>(),response.to<JsonObject>()));
  JsonDocument effective;policy.resolve("/sys/bus","system",effective.to<JsonObject>());
  assert(!std::strcmp(effective["sd_spi_mode"]|"","fixed"));
  assert((effective["sd_spi_hz"]|0u)==40000000u);
  input.clear();response.clear();
  deserializeJson(input,R"({"scope":"app:test","changes":[{"resource":"/sys/bus","settings":{"display_spi_hz":8000000}}]})");
  assert(!std::strcmp(policy.apply(input.as<JsonObjectConst>(),response.to<JsonObject>()),"setting_requires_system_scope"));
}
