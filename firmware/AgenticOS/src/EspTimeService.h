// Included by ESP adapters after Core/Preferences/network initialization helpers.
static std::atomic<int64_t> timeCompleted{0};
static std::atomic<bool> timeWasSet{false};
class EspTimeBackend:public TimeSyncBackend {
 TimeSyncConfig live_;
public:
 const char*start(const TimeSyncConfig&c)override{live_=c;timeCompleted=0;esp_sntp_set_time_sync_notification_cb([](timeval*t){timeWasSet=true;timeCompleted=t->tv_sec;});
  // configTime owns the TCP/IP locking. The server strings stay alive until stop.
  configTime(0,0,live_.servers[0],live_.servers[1],live_.servers[2]);return nullptr;}
 void stop()override{esp_sntp_stop();timeCompleted=0;}
 int64_t completion()override{return timeCompleted.exchange(0);}
};
static EspTimeBackend timeBackend;static TimeSync timeService(core,timeBackend);
static bool persistTime(const char*s,size_t n){return preferences.putString("time_sync",s)==n;}
static const char*timeHandler(const char*m,JsonObjectConst a,JsonObject o){return timeService.invoke(m,a,o,uptime(),persistTime);}
