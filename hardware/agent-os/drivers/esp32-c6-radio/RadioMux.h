#pragma once
#include <stdint.h>
// This is selected exactly once, before either protocol stack is initialized.
extern "C" void notes_radio_select_at_boot(uint8_t mode); // 0 off, 1 Thread, 2 Zigbee
extern "C" uint8_t notes_radio_boot_mode();
