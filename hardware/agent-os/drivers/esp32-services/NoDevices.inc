// A headless composition has no peripheral services to register.
static void beginNativeDevices(){}
static void registerNativeDevices(){}
static void pollNativeDevices(){}
static const char*defaultNfsExport(){return "/flash";}
static const char*defaultNfsProvider(){return "flash";}
static const char*nativeStorage(const char*m,JsonObjectConst a,JsonObject o){return flashVolume.invoke(m,a,o);}
