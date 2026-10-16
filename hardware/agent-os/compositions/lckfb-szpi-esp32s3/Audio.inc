static const char*audioAmp(bool on){return expander.write(2,on?2:0);}
static audio::EverestDuplex audioHardware(wireI2c.bus,{pins::AUDIO_MCLK,pins::AUDIO_BCLK,pins::AUDIO_WS,pins::AUDIO_DIN,pins::AUDIO_DOUT},audioAmp);
static audio::Playback playback(core,audioHardware);
static std::mutex audioMutex;
static std::atomic<unsigned>audioUsers{0};static std::atomic<bool>audioWorkerActive{false};static bool audioStopping=false;static unsigned audioRate=16000;static uint32_t audioReservation=0;static char audioFailure[80]{};
static const char*requestAudio(unsigned,unsigned);static void releaseMic(){audioUsers.fetch_and(~1u);}static const char*startMic(unsigned rate){return requestAudio(1,rate);}
static audio::CompressedCapture microphone(core,bootId,startMic,releaseMic,"/dev/mic","MMR");
static const char*speechAcquire(unsigned rate,const char*roles){if(strcmp(roles,"M"))return "capture_layout_unavailable";return requestAudio(4,rate);}static void speechRelease(){audioUsers.fetch_and(~4u);}
static audio::EnergyActivity activityDetector;
static audio::EspWakeNet wakeDetector("wakenet9.hiesp","wn9_hiesp","Hi ESP",24576,524288);
static audio::SpeechBackend*speechBackends[]={&activityDetector,&wakeDetector};
static audio::SpeechService speechService(core,"/dev/speech",speechBackends,2,speechAcquire,speechRelease);
struct AudioLevels{uint64_t frames=0;double squares[3]{};int peaks[3]{};};static std::mutex audioLevelsMutex;static AudioLevels audioLevels;
static void audioWorker(void*){
 const char*error=audioHardware.initialize(audioRate);int16_t pcm[480],mono[160];
 while(!error){
  {std::lock_guard<std::mutex>lock(audioMutex);if(!audioUsers.load()){audioStopping=true;break;}}
  if(audioUsers.load()&2){playback.pump();if(!playback.active())audioUsers.fetch_and(~2u);}
  size_t frames=0;error=audioHardware.receive(pcm,160,frames);if(error)break;
  if(audioUsers.load()&1)microphone.process({pcm,frames,audioRate,"MMR"});
  if(frames){for(size_t i=0;i<frames;++i)mono[i]=pcm[i*3];{std::lock_guard<std::mutex>lock(audioLevelsMutex);audioLevels.frames+=frames;for(size_t i=0;i<frames;++i)for(unsigned c=0;c<3;++c){int v=pcm[i*3+c];audioLevels.squares[c]+=double(v)*v;audioLevels.peaks[c]=std::max(audioLevels.peaks[c],std::abs(v));}}
   if(audioUsers.load()&4)speechService.process({mono,frames,audioRate,"M"});}
  if(audioUsers.load()&2)playback.pump();
  vTaskDelay(1);
 }
 if(error){{std::lock_guard<std::mutex>lock(audioMutex);copy(audioFailure,sizeof audioFailure,error);}microphone.failed(audioFailure);speechService.inputFailed(audioFailure);playback.failed(audioFailure);}
 audioHardware.shutdown();{std::lock_guard<std::mutex>lock(audioMutex);audioUsers=0;audioStopping=false;audioWorkerActive=false;core.resources().release(audioReservation);audioReservation=0;}vTaskDelete(nullptr);
}
static const char*requestAudio(unsigned user,unsigned rate){if(auto e=audioHardware.validate({rate,1}))return e;std::lock_guard<std::mutex>lock(audioMutex);if(audioStopping)return "capture_stopping";if(audioWorkerActive){if(audioRate!=rate)return "capture_format_conflict";audioUsers.fetch_or(user);return nullptr;}
 audioReservation=core.resources().acquire(32768,0,ResourceBudget::Region::Internal);if(!audioReservation)return "audio_memory_budget";audioUsers=user;audioRate=rate;audioWorkerActive=true;audioFailure[0]=0;{std::lock_guard<std::mutex>levels(audioLevelsMutex);audioLevels={};}
 if(xTaskCreate(audioWorker,"wish-duplex",12288,nullptr,1,nullptr)!=pdPASS){audioUsers=0;audioWorkerActive=false;core.resources().release(audioReservation);audioReservation=0;return "audio_worker_failed";}core.resources().used(audioReservation,32768);return nullptr;
}
static const char*speakerHandler(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"start")){std::lock_guard<std::mutex>lock(audioMutex);if(audioWorkerActive&&audioRate!=(a["sample_rate_hz"]|16000u))return "capture_format_conflict";}
 auto e=playback.invoke(m,a,o);if(!e&&!strcmp(m,"start")){e=requestAudio(2,a["sample_rate_hz"]|16000u);if(e)playback.failed(e);}return e;
}
static const char*audioStatus(const char*m,JsonObjectConst a,JsonObject o){if(strcmp(m,"read"))return "unsupported_method";if(a.size())return "unknown_audio_argument";std::lock_guard<std::mutex>stateLock(audioMutex);o["running"]=audioWorkerActive.load();o["users"]=audioUsers.load();o["sample_rate_hz"]=audioRate;o["error"]=audioFailure;o["native_state"]=audioHardware.stage();std::lock_guard<std::mutex>lock(audioLevelsMutex);o["frames"]=audioLevels.frames;auto list=o["inputs"].to<JsonArray>();for(unsigned c=0;c<3;++c){auto p=list.add<JsonObject>();p["role"]=c<2?"microphone":"playback_reference";p["peak"]=audioLevels.peaks[c];p["rms"]=audioLevels.frames?sqrt(audioLevels.squares[c]/audioLevels.frames):0;}return nullptr;}
static void registerAudio(){
 core.add({"/dev/mic","read,describe,start,stop,read_chunk,ack,release,list","compressed microphone capture",[](const char*m,JsonObjectConst a,JsonObject o){return microphone.invoke(m,a,o);},[](JsonObject o){microphone.describe(o);},"read,describe,read_chunk,list"});
 core.add({"/dev/speaker","read,describe,start,write,finish,stop,pause,resume,volume","codec speaker output",speakerHandler,[](JsonObject o){playback.describe(o);},"read,describe"});
 core.add({"/dev/speech","read,describe,start,stop","speech detection",[](const char*m,JsonObjectConst a,JsonObject o){return speechService.invoke(m,a,o);},[](JsonObject o){speechService.describe(o);},"read,describe"});
 core.add({"/dev/audio","read","shared-clock audio status",audioStatus,nullptr,"read"});
}
