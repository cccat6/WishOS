#pragma once
#include <stdint.h>
#include <stddef.h>
namespace notesos::audio {
// Interleaved PCM. Roles describe captured signal sources, not output speakers:
// M = microphone, R = synchronous playback reference. "MMR" is not three mics.
// A compiled backend can add layouts without changing the portable resource ABI.
struct CaptureFrame {
 const int16_t*data;size_t frames;unsigned rate;const char*roles;
 uint64_t firstSample=0;
};
}
