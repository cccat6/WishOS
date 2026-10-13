#pragma once
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstring>
constexpr int O_RDONLY=1,O_RDWR=2,O_CREAT=4,O_TRUNC=8,O_EXCL=16;
inline bool fakeSdLocked=false,fakeSdReadFailure=false,fakeSdDirFailure=false;
inline unsigned fakeSdOpens=0,fakeSdWrites=0;
struct FsFile {
 bool directory=false,valid=true;
 explicit operator bool()const{return valid;}
 uint64_t fileSize(){return 100;}
 bool isDir(){return directory;}
 bool truncate(uint64_t){return true;}
 bool seekSet(uint64_t){return true;}
 int read(uint8_t*,size_t n){assert(fakeSdLocked);return fakeSdReadFailure?0:int(n);}
 size_t write(const uint8_t*,size_t n){assert(fakeSdLocked);++fakeSdWrites;return n;}
 bool sync(){assert(fakeSdLocked);return true;}
 bool openNext(FsFile*,int){assert(fakeSdLocked);return false;}
 bool getError(){return fakeSdDirFailure;}
 void close(){}
 size_t getName(char*out,size_t cap){const char*name="test";if(cap<5)return 0;strcpy(out,name);return 4;}
};
struct SdFs {
 int32_t freeClusterCount(){return 10;}
 uint32_t bytesPerCluster(){return 4096;}
 int fatType()const{return 32;}
 FsFile open(const char*p,int){assert(fakeSdLocked);++fakeSdOpens;FsFile f;f.directory=!strcmp(p,"/");return f;}
 bool rmdir(const char*){assert(fakeSdLocked);++fakeSdWrites;return true;}
 bool remove(const char*){assert(fakeSdLocked);++fakeSdWrites;return true;}
 bool mkdir(const char*,bool){assert(fakeSdLocked);++fakeSdWrites;return true;}
 bool rename(const char*,const char*){assert(fakeSdLocked);++fakeSdWrites;return true;}
};

using FsVolume=SdFs;
