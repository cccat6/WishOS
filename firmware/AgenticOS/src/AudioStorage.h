// Main-loop spooler. Capture stays in its I2S task; retention/VFS ownership stays serialized.
static uint32_t blockCRC(const uint8_t*p,size_t n){uint32_t c=~0u;while(n--){c^=*p++;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u&uint32_t(-int(c&1)));}return ~c;}
static void putLE(uint8_t*p,uint64_t value,unsigned n){while(n--){*p++=uint8_t(value);value>>=8;}}
static RecordingRetention*recordings=nullptr;
static FsFile audioPcm,audioIndex;static uint64_t audioOffset=0,audioOrder=0;static uint32_t audioBlocks=0,audioSyncedAt=0;static bool audioStorageOk=false;
static const char*loadRecordingCatalog(JsonObjectConst config){
 delete recordings;recordings=new(std::nothrow)RecordingRetention(sdFiles,vfs);if(!recordings)return "memory_low";
 RecordingPolicy policy;policy.maxBytes=config["storage_max_bytes"]|0u;policy.maxCount=config["storage_max_count"]|0u;policy.oldest=!strcmp(config["storage_on_limit"]|"stop_storage","delete_oldest");recordings->configure(policy);
 uint32_t cursor=0;
 while(true){JsonDocument page;if(auto e=sdFiles.list("/recordings",cursor,12,page.to<JsonObject>())){if(!strcmp(e,"not_directory"))return nullptr;return e;}
   for(auto item:page["entries"].as<JsonArrayConst>()){
     const char*name=item["name"]|"";size_t length=strlen(name);if(length<11||strcmp(name+length-11,".adpcm.json"))continue;
     char path[256];snprintf(path,sizeof path,"/recordings/%s",name);uint8_t text[1024];size_t got=0;if(auto e=sdFiles.read(path,0,text,sizeof text,got))return e;
     JsonDocument meta;if(deserializeJson(meta,text,got)||strcmp(meta["format"]|"","notes-recording/1"))continue;
     path[strlen(path)-5]=0;uint64_t bytes=got;FileInfo info;if(auto e=sdFiles.stat(path,info))return e;bytes+=info.size;char idx[256];snprintf(idx,sizeof idx,"%s.idx",path);if(auto e=sdFiles.stat(idx,info))return e;bytes+=info.size;
     if(auto e=recordings->remember(path,bytes,meta["order"]|uint64_t(0)))return e;
   }
   if(!(page["more"]|false))break;uint32_t next=page["next_cursor"]|0u;if(next<=cursor)return "recording_directory_error";cursor=next;
 }return nullptr;
}
static const char*beginAudioStorage(JsonObjectConst config){
 if(auto e=loadRecordingCatalog(config))return e;if(auto e=recordings->makeRoom(1024,1))return e;
 if(!takeSPI())return "spi_busy";bool ok=SD.exists("/recordings")||SD.mkdir("/recordings",true);char idx[256];snprintf(idx,sizeof idx,"%s.idx",recordingFile);
 if(ok){audioPcm=SD.open(recordingFile,O_CREAT|O_EXCL|O_WRONLY);audioIndex=SD.open(idx,O_CREAT|O_EXCL|O_WRONLY);ok=bool(audioPcm)&&bool(audioIndex);}
 if(ok){copy(sdFiles.reserved,sizeof sdFiles.reserved,recordingFile);uint8_t header[16]={'N','A','I','1'};putLE(header+4,micRate,4);putLE(header+8,32,4);ok=audioIndex.write(header,16)==16;}
 if(!ok){audioPcm.close();audioIndex.close();sdFiles.reserved[0]=0;}
 giveSPI();if(!ok)return "recording_open_failed";audioOrder=recordings->nextOrder();audioOffset=0;audioBlocks=0;audioSyncedAt=millis();audioStorageOk=true;audioSpooling=true;return nullptr;
}
static void pollAudioStorage(){
 if(!audioSpooling)return;
 for(int i=0;i<4;++i){AudioChunk chunk;if(xQueuePeek(spoolQueue,&chunk,0)!=pdTRUE)break;
   if(audioStorageOk){auto error=recordings->makeRoom(audioOffset+uint64_t(audioBlocks)*32+1024+chunk.bytes+32,1);if(error){audioStorageOk=false;audioStorageError=error;if(audioStopOnStorageError)audioStop=true;}}
   if(!audioStorageOk){xQueueReceive(spoolQueue,&chunk,0);audioStorageDrops+=chunk.samples;continue;}
   if(!takeSPI())return;xQueueReceive(spoolQueue,&chunk,0);uint8_t entry[32]{};putLE(entry,chunk.sequence,4);putLE(entry+4,chunk.sampleStart,8);putLE(entry+12,audioOffset,8);putLE(entry+20,chunk.bytes,4);putLE(entry+24,blockCRC(chunk.data,chunk.bytes),4);
   bool ok=audioPcm.write(chunk.data,chunk.bytes)==size_t(chunk.bytes)&&audioIndex.write(entry,32)==32;
   if(ok){audioOffset+=chunk.bytes;++audioBlocks;}if(ok&&uint32_t(millis()-audioSyncedAt)>=1000){ok=audioPcm.sync()&&audioIndex.sync();audioSyncedAt=millis();}giveSPI();
   if(!ok){audioStorageOk=false;audioStorageError="recording_write_failed";audioStorageDrops+=chunk.samples;if(audioStopOnStorageError)audioStop=true;}
 }
 if(audioRunning||uxQueueMessagesWaiting(spoolQueue))return;if(!takeSPI())return;
 bool synced=audioPcm.sync()&&audioIndex.sync();audioPcm.close();audioIndex.close();JsonDocument meta;
 meta["format"]="notes-recording/1";meta["id"]=recordingId;meta["order"]=audioOrder;meta["sample_rate_hz"]=micRate;meta["encoding"]="ima-adpcm-blocks";meta["channels"]=1;meta["bytes"]=audioOffset;meta["blocks"]=audioBlocks;meta["missing_samples"]=audioStorageDrops.load();meta["capture_failed"]=audioCaptureFailed.load();meta["capture"]=synced&&audioStorageOk&&!audioStorageDrops&&!audioCaptureFailed?"finalized":"incomplete";meta["retention"]=recordingRetention;meta["index"]="nai1-le-32";
 char path[256];snprintf(path,sizeof path,"%s.json",recordingFile);auto file=SD.open(path,O_CREAT|O_EXCL|O_WRONLY);bool stored=false;size_t n=measureJson(meta);if(file&&!meta.overflowed()){stored=serializeJson(meta,file)==n&&file.sync()&&synced;file.close();}sdFiles.reserved[0]=0;++sdFiles.revision;sdFiles.freeKnown=false;giveSPI();
 if(stored)recordings->remember(recordingFile,audioOffset+16+uint64_t(audioBlocks)*32+n,audioOrder);else audioStorageError="recording_not_fully_committed";
 JsonDocument receipt;receipt["recording_id"]=recordingId;receipt["owner"]=micOwner;
 receipt["file"]=String("/storage/user")+recordingFile;receipt["durable"]=stored;
 receipt["complete"]=stored&&audioStorageOk&&!audioStorageDrops&&!audioCaptureFailed;
 receipt["capture_failed"]=audioCaptureFailed.load();receipt["missing_samples"]=audioStorageDrops.load();receipt["bytes"]=audioOffset;
 receipt["error"]=audioStorageError.load();emit("/dev/mic",stored?"stored":"storage_failed",receipt.as<JsonObjectConst>(),micOwner);audioSpooling=false;
}
