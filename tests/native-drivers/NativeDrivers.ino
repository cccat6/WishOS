// Compile-only SDK compatibility fixture. Never uploaded by the test runner.
#include "Core.h"
#include "drivers/esp-wire-i2c/WireI2c.h"
#include "drivers/gxhtc3/Gxhtc3.h"
#include "drivers/qmc5883l/Qmc5883l.h"
#include "drivers/qmi8658/Qmi8658.h"
#include "drivers/ft6336/Ft6336.h"
#include "drivers/pca9557/Pca9557.h"
#include "drivers/esp-wakenet/EspWakeNet.h"
#include "drivers/esp-i2s-input/EspI2sInput.h"
#include "drivers/esp-i2s-output/EspI2sOutput.h"
using namespace notesos;
using namespace notesos::drivers;
static WireI2c bus(Wire,nullptr,100000);
static Gxhtc3 temperature(bus.bus);
static Qmc5883l magnetic(bus.bus);
static Qmi8658 motion(bus.bus,Qmi8658::Profile::RevA);
static Ft6336 touch(bus.bus);
static SampleBackend* sensors[]={&temperature,&magnetic,&motion,&touch};
static audio::EspWakeNet wake("fixture.wakenet",WISHOS_WAKE_MODEL,"Hi ESP",65536,
#if CONFIG_IDF_TARGET_ESP32S3
524288
#else
0
#endif
);
void setup() {
 JsonDocument description;for(auto backend:sensors)backend->describe(description.to<JsonObject>());
 // Force the selected archive/model ABI to link. This sketch is never uploaded;
 // no success claim about acoustic detection follows from this fixture.
 ResourceBudget budget;wake.start(JsonObjectConst(),budget);wake.stop();
 audio::EspI2sInput input({I2S_NUM_0,GPIO_NUM_4,GPIO_NUM_5,GPIO_NUM_6});
 input.describe(description.to<JsonObject>());input.initialize(16000);input.shutdown();
 audio::EspI2sOutput output(I2S_NUM_0,GPIO_NUM_4,GPIO_NUM_5,GPIO_NUM_6);
 output.open({16000,1});output.close();
}
void loop() {}
