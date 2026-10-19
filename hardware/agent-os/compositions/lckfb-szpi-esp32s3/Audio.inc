// Native worker only: retry an arbiter rejection, which performed no I/O.
static const char*audioAmp(bool on){for(unsigned n=0;n<100;++n){auto e=expander.write(2,on?2:0);if(!e||strcmp(e,"bus_busy"))return e;vTaskDelay(1);}return "bus_busy";}
static audio::EverestDuplex audioHardware(wireI2c.bus,{pins::AUDIO_MCLK,pins::AUDIO_BCLK,pins::AUDIO_WS,pins::AUDIO_DIN,pins::AUDIO_DOUT},audioAmp);
static audio::Playback playback(core,audioHardware);
static std::mutex audioMutex;
static constexpr unsigned AUDIO_WORKER_STACK_BYTES=8192,AUDIO_INTERNAL_WORKSPACE_BYTES=28672;
static std::atomic<uint32_t>audioStackFree{0},recordingStackFree{0};
static bool audioPending=false;static uint64_t audioStartDeadline=0;
static std::atomic<unsigned>audioUsers{0};static std::atomic<bool>audioWorkerActive{false};static bool audioStopping=false;static unsigned audioRate=16000;static uint32_t audioReservation=0;static char audioFailure[80]{};
static const char*requestAudio(unsigned,unsigned);static void releaseMic(){audioUsers.fetch_and(~1u);}static const char*startMic(unsigned rate){return requestAudio(1,rate);}
static audio::CompressedCapture microphone(core,bootId,startMic,releaseMic,"/dev/mic","MMR");
struct RecordingTask{void(*run)(void*);void*context;};
static void recordingTask(void*p){auto e=*static_cast<RecordingTask*>(p);delete static_cast<RecordingTask*>(p);e.run(e.context);recordingStackFree=uxTaskGetStackHighWaterMark(nullptr);vTaskDelete(nullptr);}
static audio::RecordingStorage microphoneStorage(core,vfs,sdVolume.provider(),{[](void(*run)(void*),void*context){auto entry=new(std::nothrow)RecordingTask{run,context};if(!entry)return false;if(xTaskCreate(recordingTask,"wish-recording",8192,entry,1,nullptr)!=pdPASS){delete entry;return false;}return true;},12288},&microphone,[](void*p,const char*id,const char*file,bool pending,bool durable,const char*error,uint64_t missing){static_cast<audio::CompressedCapture*>(p)->storageStatus(id,file,pending,durable,error,missing);});
static const char*speechAcquire(unsigned rate,const char*roles){if(strcmp(roles,"M"))return "capture_layout_unavailable";return requestAudio(4,rate);}static void speechRelease(){audioUsers.fetch_and(~4u);}
static audio::EnergyActivity activityDetector;
static audio::EspWakeNet wakeDetector("wakenet9.hiesp","wn9_hiesp","Hi ESP",24576,524288);
static audio::SpeechBackend*speechBackends[]={&activityDetector,&wakeDetector};
static audio::SpeechService speechService(core,"/dev/speech",speechBackends,2,speechAcquire,speechRelease);
struct AudioLevels{uint64_t frames=0;double squares[3]{};int peaks[3]{};};static std::mutex audioLevelsMutex;static AudioLevels audioLevels;
static void audioWorker(void*){
 const char*error=audioHardware.initialize(audioRate);int16_t pcm[480],mono[160];audioStackFree=uxTaskGetStackHighWaterMark(nullptr);uint32_t stackMeasuredAt=millis();
 while(!error){
  {std::lock_guard<std::mutex>lock(audioMutex);if(!audioUsers.load()){audioStopping=true;break;}}
  if(audioUsers.load()&2){playback.pump();if(!playback.active())audioUsers.fetch_and(~2u);}
  size_t frames=0;error=audioHardware.receive(pcm,160,frames);if(error)break;
  if(audioUsers.load()&1)microphone.process({pcm,frames,audioRate,"MMR"});
  if(frames){for(size_t i=0;i<frames;++i)mono[i]=pcm[i*3];{std::lock_guard<std::mutex>lock(audioLevelsMutex);audioLevels.frames+=frames;for(size_t i=0;i<frames;++i)for(unsigned c=0;c<3;++c){int v=pcm[i*3+c];audioLevels.squares[c]+=double(v)*v;audioLevels.peaks[c]=std::max(audioLevels.peaks[c],std::abs(v));}}
   if(audioUsers.load()&4)speechService.process({mono,frames,audioRate,"M"});}
  if(audioUsers.load()&2)playback.pump();
  if(uint32_t(millis()-stackMeasuredAt)>=1000){audioStackFree=uxTaskGetStackHighWaterMark(nullptr);stackMeasuredAt=millis();}
  vTaskDelay(1);
 }
 if(error){{std::lock_guard<std::mutex>lock(audioMutex);audioStopping=true;copy(audioFailure,sizeof audioFailure,error);}microphone.failed(audioFailure);speechService.inputFailed(audioFailure);playback.failed(audioFailure);}
 audioHardware.shutdown();audioStackFree=uxTaskGetStackHighWaterMark(nullptr);{std::lock_guard<std::mutex>lock(audioMutex);audioUsers=0;audioStopping=false;audioWorkerActive=false;core.resources().release(audioReservation);audioReservation=0;}vTaskDelete(nullptr);
}
// Accepted input/output can wait for transient native work to release internal
// memory. The deadline is adapter policy; readiness remains the first real frame.
static const char*launchAudioLocked(){
 if(!core.resources().canAllocate(AUDIO_INTERNAL_WORKSPACE_BYTES,ResourceBudget::Region::Internal))return "audio_memory_budget";
 audioReservation=core.resources().acquire(AUDIO_INTERNAL_WORKSPACE_BYTES,0,ResourceBudget::Region::Internal);if(!audioReservation)return "audio_memory_budget";
 audioWorkerActive=true;audioFailure[0]=0;{std::lock_guard<std::mutex>levels(audioLevelsMutex);audioLevels={};}
 if(xTaskCreate(audioWorker,"wish-duplex",AUDIO_WORKER_STACK_BYTES,nullptr,1,nullptr)!=pdPASS){audioWorkerActive=false;core.resources().release(audioReservation);audioReservation=0;return "audio_worker_failed";}core.resources().used(audioReservation,AUDIO_INTERNAL_WORKSPACE_BYTES);return nullptr;
}
static const char*requestAudio(unsigned user,unsigned rate){if(auto e=audioHardware.validate({rate,1}))return e;std::lock_guard<std::mutex>lock(audioMutex);if(audioStopping)return "capture_stopping";if(audioWorkerActive||audioPending){if(audioRate!=rate)return "capture_format_conflict";audioUsers.fetch_or(user);return nullptr;}
 audioRate=rate;audioUsers=user;auto error=launchAudioLocked();if(error&&!strcmp(error,"audio_memory_budget")){audioPending=true;audioStartDeadline=uptime()+policies.get<uint32_t>("/dev/audio","start_timeout_ms",3000);return nullptr;}if(error)audioUsers=0;return error;
}
static void pollAudio(){const char*error=nullptr;{std::lock_guard<std::mutex>lock(audioMutex);if(!audioPending)return;if((audioUsers.load()&2)&&!playback.active())audioUsers.fetch_and(~2u);if(!audioUsers.load()){audioPending=false;return;}
 error=launchAudioLocked();if(!error){audioPending=false;return;}if(!strcmp(error,"audio_memory_budget")&&uptime()<audioStartDeadline)return;audioPending=false;audioUsers=0;copy(audioFailure,sizeof audioFailure,error);}
 microphone.failed(error);speechService.inputFailed(error);playback.failed(error);
}
static const char*speakerHandler(const char*m,JsonObjectConst a,JsonObject o){if(!strcmp(m,"start")){std::lock_guard<std::mutex>lock(audioMutex);if(audioWorkerActive&&audioRate!=(a["sample_rate_hz"]|16000u))return "capture_format_conflict";}
 auto e=playback.invoke(m,a,o);if(!e&&!strcmp(m,"start")){e=requestAudio(2,a["sample_rate_hz"]|16000u);if(e)playback.failed(e);}return e;
}
static const char*audioStatus(const char*m,JsonObjectConst a,JsonObject o){if(strcmp(m,"read"))return "unsupported_method";if(a.size())return "unknown_audio_argument";std::lock_guard<std::mutex>stateLock(audioMutex);o["running"]=audioWorkerActive.load();o["starting"]=audioPending;o["start_timeout_ms"]=policies.get<uint32_t>("/dev/audio","start_timeout_ms",3000);o["users"]=audioUsers.load();o["sample_rate_hz"]=audioRate;o["error"]=audioFailure;o["native_state"]=audioHardware.stage();o["worker_stack_bytes"]=AUDIO_WORKER_STACK_BYTES;o["worker_stack_free_bytes"]=audioStackFree.load();o["recording_worker_stack_bytes"]=8192;o["recording_worker_stack_free_bytes"]=recordingStackFree.load();std::lock_guard<std::mutex>lock(audioLevelsMutex);o["frames"]=audioLevels.frames;auto list=o["inputs"].to<JsonArray>();for(unsigned c=0;c<3;++c){auto p=list.add<JsonObject>();p["role"]=c<2?"microphone":"playback_reference";p["peak"]=audioLevels.peaks[c];p["rms"]=audioLevels.frames?sqrt(audioLevels.squares[c]/audioLevels.frames):0;}return nullptr;}
static void registerAudio(){
 microphone.storage(microphoneStorage);
 core.add({"/dev/mic","read,describe,start,stop,read_chunk,read_batch,ack,release,list","compressed microphone capture",[](const char*m,JsonObjectConst a,JsonObject o){return microphone.invoke(m,a,o);},[](JsonObject o){microphone.describe(o);},audio::CompressedCapture::ReadMethods});
 core.add({"/dev/speaker","read,describe,start,write,finish,stop,pause,resume,volume","codec speaker output",speakerHandler,[](JsonObject o){playback.describe(o);},"read,describe"});
 core.add({"/dev/speech","read,describe,start,stop","speech detection",[](const char*m,JsonObjectConst a,JsonObject o){return speechService.invoke(m,a,o);},[](JsonObject o){speechService.describe(o);},"read,describe"});
 core.add({"/dev/audio","read","shared-clock audio status",audioStatus,nullptr,"read"});
}
