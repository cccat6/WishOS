#include "src/Hardware.h"
#include "src/BoardPackage.h"
size_t getArduinoLoopTaskStackSize(){return notesos::boardpack::SYSTEM_STACK_BYTES;}
void setup() { notesos::begin(); }
void loop() { notesos::poll(); }
