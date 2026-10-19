#include <cassert>
#include "drivers/esp-camera/EspCameraClock.h"
int main(){using notesos::drivers::EspCameraClock;static_assert(!EspCameraClock::exactSupported());EspCameraClock clock;assert(!clock.validate(25000000));assert(!clock.apply(25000000));struct NoClockMetadata{};clock.read(NoClockMetadata{});clock.release();}
