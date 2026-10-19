// This device has independent microphone and amplifier clocks/controllers.
static audio::EspI2sInput microphoneHardware({I2S_NUM_1,gpio_num_t(pins::MIC_BCLK),gpio_num_t(pins::MIC_WS),gpio_num_t(pins::MIC_DATA),16});
static audio::EspI2sOutput speakerHardware(I2S_NUM_0,gpio_num_t(pins::SPK_BCLK),gpio_num_t(pins::SPK_WS),gpio_num_t(pins::SPK_DATA));
static audio::Playback playback(core,speakerHardware);
static std::mutex inputMutex,outputMutex;
static std::atomic<unsigned>inputUsers{0};static std::atomic<bool>inputWorkerActive{false},outputWorkerActive{false};
static unsigned inputRate=16000;static bool inputClosing=false;static uint32_t inputReservation=0,outputReservation=0;
static char inputError[80]{};
static const char*acquireInput(unsigned,unsigned);
static const char*beginMicrophone(unsigned rate){return acquireInput(1,rate);}
static void releaseMicrophone(){std::lock_guard<std::mutex>lock(inputMutex);inputUsers.fetch_and(~1u);}
static audio::CompressedCapture microphone(core,bootId,beginMicrophone,releaseMicrophone);
static const char*beginSpeech(unsigned rate,const char*roles){return strcmp(roles,"M")?"capture_layout_unavailable":acquireInput(2,rate);}
static void releaseSpeech(){std::lock_guard<std::mutex>lock(inputMutex);inputUsers.fetch_and(~2u);}
static audio::EnergyActivity activityDetector;
static audio::EspWakeNet wakeDetector("wakenet9.hiesp","wn9_hiesp","Hi ESP",24576,524288);
static audio::SpeechBackend*speechBackends[]={&activityDetector,&wakeDetector};
static audio::SpeechService speechService(core,"/dev/speech",speechBackends,2,beginSpeech,releaseSpeech);
static void inputTask(void*){
 const char*error=microphoneHardware.initialize(inputRate);int16_t pcm[audio::EspI2sInput::BlockFrames];
 while(!error){
  {std::lock_guard<std::mutex>lock(inputMutex);if(!inputUsers.load()){inputClosing=true;break;}}
  size_t frames=0;error=microphoneHardware.receive(pcm,audio::EspI2sInput::BlockFrames,frames);if(error)break;
  if(inputUsers.load()&1)microphone.process({pcm,frames,inputRate,"M"});
  if(inputUsers.load()&2)speechService.process({pcm,frames,inputRate,"M"});
  vTaskDelay(1);
 }
 {std::lock_guard<std::mutex>lock(inputMutex);inputClosing=true;if(error)copy(inputError,sizeof inputError,error);}
 if(error){microphone.failed(inputError);speechService.inputFailed(inputError);}
 auto closed=microphoneHardware.shutdown();
 {std::lock_guard<std::mutex>lock(inputMutex);if(closed)copy(inputError,sizeof inputError,closed);inputUsers=0;inputClosing=false;inputWorkerActive=false;if(!closed){core.resources().release(inputReservation);inputReservation=0;}}
 vTaskDelete(nullptr);
}
static const char*acquireInput(unsigned user,unsigned rate){
 if(!audio::EspI2sInput::supports(rate))return "audio_sample_rate";std::lock_guard<std::mutex>lock(inputMutex);
 if(inputClosing)return "capture_stopping";
 if(inputWorkerActive){if(rate!=inputRate)return "capture_format_conflict";inputUsers.fetch_or(user);return nullptr;}
 if(inputReservation)return "audio_recovery_required";
 inputReservation=core.resources().acquire(16384,0,ResourceBudget::Region::Internal);if(!inputReservation)return "audio_memory_budget";
 inputRate=rate;inputUsers=user;inputError[0]=0;inputWorkerActive=true;core.resources().used(inputReservation,16384);
 if(xTaskCreate(inputTask,"wish-mic",8192,nullptr,1,nullptr)!=pdPASS){inputWorkerActive=false;inputUsers=0;core.resources().release(inputReservation);inputReservation=0;return "audio_worker_failed";}
 return nullptr;
}
static void outputTask(void*){
 while(true){
  playback.pump();{std::lock_guard<std::mutex>lock(outputMutex);
  if(!playback.active()){outputWorkerActive=false;core.resources().release(outputReservation);outputReservation=0;break;}}
  // The backend is nonblocking; leave other native work a scheduling turn.
  vTaskDelay(1);
 }
 vTaskDelete(nullptr);
}
static const char*speakerHandler(const char*method,JsonObjectConst args,JsonObject out){
 std::lock_guard<std::mutex>lock(outputMutex);auto error=playback.invoke(method,args,out);if(error||strcmp(method,"start")||outputWorkerActive)return error;
 outputReservation=core.resources().acquire(12288,0,ResourceBudget::Region::Internal);
 if(!outputReservation){playback.failed("audio_memory_budget");return "audio_memory_budget";}
 core.resources().used(outputReservation,12288);outputWorkerActive=true;
 if(xTaskCreate(outputTask,"wish-speaker",6144,nullptr,1,nullptr)!=pdPASS){outputWorkerActive=false;core.resources().release(outputReservation);outputReservation=0;playback.failed("audio_worker_failed");return "audio_worker_failed";}
 return nullptr;
}
static void registerAudio(){
 core.add({"/dev/mic","read,describe,start,stop,read_chunk,read_batch,ack,release,list","compressed microphone capture",[](const char*m,JsonObjectConst a,JsonObject o){return microphone.invoke(m,a,o);},[](JsonObject o){microphoneHardware.describe(o);microphone.describe(o);},audio::CompressedCapture::ReadMethods});
 core.add({"/dev/speaker","read,describe,start,write,finish,stop,pause,resume,volume","I2S amplifier output",speakerHandler,[](JsonObject o){playback.describe(o);},"read,describe"});
 core.add({"/dev/speech","read,describe,start,stop","speech detection",[](const char*m,JsonObjectConst a,JsonObject o){return speechService.invoke(m,a,o);},[](JsonObject o){speechService.describe(o);},"read,describe"});
 core.add({"/dev/audio","read","independent input and output clocks",[](const char*,JsonObjectConst,JsonObject o)->const char*{std::lock_guard<std::mutex>lock(inputMutex);o["capture_running"]=inputWorkerActive.load();o["playback_running"]=outputWorkerActive.load();o["input_roles"]="M";o["independent_sample_rates"]=true;o["input_error"]=inputError;return nullptr;},nullptr,"read"});
}
