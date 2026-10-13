#include "SdProvider.h"
#include "NfsProvider.h"
#include <cassert>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
using namespace notesos;
struct Disk:FsBlockDeviceInterface {
 int fd;static constexpr uint32_t sectors=16*1024*1024;
 Disk(){char path[]="/tmp/notes-fat-XXXXXX";fd=mkstemp(path);assert(fd>=0);unlink(path);assert(!ftruncate(fd,uint64_t(sectors)*512));}~Disk(){close(fd);}
 bool isBusy()override{return false;}uint32_t sectorCount()override{return sectors;}bool syncDevice()override{return fsync(fd)==0;}
 bool readSector(uint32_t s,uint8_t*p)override{return readSectors(s,p,1);}bool writeSector(uint32_t s,const uint8_t*p)override{return writeSectors(s,p,1);}
 bool readSectors(uint32_t s,uint8_t*p,size_t n)override{return s<=sectors&&n<=sectors-s&&pread(fd,p,n*512,uint64_t(s)*512)==ssize_t(n*512);}
 bool writeSectors(uint32_t s,const uint8_t*p,size_t n)override{return s<=sectors&&n<=sectors-s&&pwrite(fd,p,n*512,uint64_t(s)*512)==ssize_t(n*512);}
};
static bool locked=false;static bool take(){assert(!locked);return locked=true;}static void give(){assert(locked);locked=false;}static const char*ensure(){assert(locked);return nullptr;}
int main(){for(bool exfat:{false,true}){
 Disk disk;uint8_t scratch[512];if(exfat){ExFatFormatter f;assert(f.format(&disk,scratch,nullptr));}else{FatFormatter f;assert(f.format(&disk,scratch,nullptr));}
 FsVolume volume;assert(volume.begin(&disk,false,1));bool mounted=true;SdProvider provider(volume,mounted,take,give,ensure);Vfs v;v.add(provider);NfsProviderStorage storage(provider,v);esp32_nfs::Memory memory({65536,0});esp32_nfs::PathVfs::Registry registry(memory);NfsProviderVfs fs(storage,provider,registry);
 using namespace esp32_nfs;uint64_t dir,file;SetAttr attrs;assert(fs.create(fs.root(),"书籍",true,1,0,attrs,dir)==Ok);assert(fs.create(dir,"阅读-Ä.txt",false,1,0,attrs,file)==Ok);
 uint8_t bytes[]={0,1,255};size_t n=3;assert(fs.write(file,0,bytes,n)==Ok&&n==3);uint64_t alias;assert(fs.lookup(dir,"阅读-ä.TXT",alias)==Ok&&alias==file);
 uint8_t actual[3];n=3;assert(fs.read(alias,0,actual,n)==Ok&&n==3&&!memcmp(bytes,actual,3));
 assert(v.lockRead("/storage/user/书籍/阅读-ä.TXT",7));n=3;assert(fs.write(file,0,bytes,n)==Jukebox);v.release(7);
 assert(fs.rename(dir,"阅读-Ä.txt",dir,"新的阅读.txt")==Ok);Attr info;assert(fs.getattr(file,info)==Ok&&info.size==3);char path[256];assert(fs.canonicalPath(file,path,sizeof path)==Ok&&!strcmp(path,"/书籍/新的阅读.txt"));
 assert(fs.endSession()==Ok&&memory.stats().usedBytes==0);volume.end();std::cout<<(exfat?"exFAT":"FAT32")<<" real image: Chinese create/read/write/rename, Unicode case identity and OS lease protection passed\n";
}}
