#pragma once
#include "PosixFileProvider.h"
#include <LittleFS.h>
namespace notesos {
class LittleFsProvider:public PosixFileProvider {
 fs::LittleFSFS&fs_;
public:
 LittleFsProvider(fs::LittleFSFS&fs,bool&mounted,const char*base,const char*mount="/storage/flash"):PosixFileProvider("flash",mount,"internal_flash",false,mounted,base,"/dev/flash","littlefs",4096),fs_(fs){}
 const char*captureSpace(){uint64_t total=fs_.totalBytes(),used=fs_.usedBytes();if(used>total)return "space_failed";PosixFileProvider::captureSpace(total,total-used);return nullptr;}
};
}
