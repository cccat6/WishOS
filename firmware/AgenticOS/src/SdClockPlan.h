#pragma once
#include <stddef.h>
#include <stdint.h>

namespace notesos {
template <typename Effective>
bool sdClockExact(uint32_t requested, Effective effective) {
  return effective(requested)==requested;
}
// Enumerate attainable divider steps rather than guessing a few clock values.
// The adapter supplies its SPI source clock and its divider mapping.
template <typename Effective>
size_t sdClockCandidates(uint32_t sourceHz,uint32_t ceiling, Effective effective,
                         uint32_t *requests, uint32_t *actual, size_t capacity) {
  size_t count = 0;
  if(!sourceHz||!capacity)return 0;
  for(uint32_t divisor=1;divisor<=sourceHz/1000000u;++divisor){
    uint32_t requested=sourceHz/divisor;
    if(requested>ceiling)continue;
    uint32_t hz = effective(requested);
    if (hz<1000000u || hz>ceiling) continue;
    bool duplicate = false;
    for (size_t i = 0; i < count; ++i)
      if (actual[i] == hz) duplicate = true;
    if (duplicate) continue;
    if (count == capacity) break;
    requests[count] = requested;
    actual[count++] = hz;
  }
  return count;
}
} // namespace notesos
