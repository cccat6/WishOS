#include "drivers/sd-spi-volume/SdProvider.h"
#include <cassert>
#include <iostream>
using namespace notesos;
static bool mounted=false,allowLock=true,readOnlyOnMount=false;
static SdProvider*provider;static bool maintenance=false;
static bool working(){return maintenance;}
static bool take(){if(!allowLock)return false;assert(!fakeSdLocked);return fakeSdLocked=true;}
static void give(){assert(fakeSdLocked);fakeSdLocked=false;}
static const char*ready(){assert(fakeSdLocked);mounted=true;if(readOnlyOnMount)provider->writable=false;return nullptr;}
int main(){
 SdFs sd;SdProvider files(sd,mounted,take,give,ready);provider=&files;JsonDocument d;
 files.describe(d.to<JsonObject>());assert(!fakeSdOpens&&!mounted);
 mounted=true;assert(take());files.captureMount(10000,8000);give();files.describe(d.to<JsonObject>());
 assert(!fakeSdOpens&&d["physical_capacity_bytes"]=="10000");
 uint8_t bytes[4]{};size_t n=0;
 mounted=false;readOnlyOnMount=true;files.writable=true;
 assert(!strcmp(files.write("/x",0,bytes,4,false,n),"read_only"));assert(!fakeSdOpens&&!fakeSdWrites&&!fakeSdLocked);
 for(int op=0;op<3;++op){mounted=false;files.writable=true;const char*e=op==0?files.remove("/x"):op==1?files.mkdir("/x"):files.rename("/x","/y");assert(e&&!strcmp(e,"read_only"));}
 assert(!fakeSdWrites&&!fakeSdOpens);
 readOnlyOnMount=false;files.writable=true;strcpy(files.reserved,"/recordings/a.pcm");
 assert(files.protectedPath("/RECORDINGS/A.PCM"));
 assert(files.write("/recordings/a.pcm.json",0,bytes,4,false,n));assert(files.remove("/recordings"));
 assert(!fakeSdOpens&&!fakeSdWrites);
 assert(!files.write("/unrelated",0,bytes,4,false,n)&&n==4&&!fakeSdLocked);
 fakeSdReadFailure=true;assert(!strcmp(files.read("/x",0,bytes,4,n),"read_failed"));
 assert(!files.read("/x",100,bytes,4,n)&&n==0); // EOF is distinct from a failed transfer.
 fakeSdDirFailure=true;assert(!strcmp(files.list("/",0,10,d.to<JsonObject>()),"directory_read_failed"));
 allowLock=false;auto before=fakeSdOpens;assert(!strcmp(files.read("/x",0,bytes,4,n),"spi_busy")&&before==fakeSdOpens);
 assert(files.sameName("目录/Ä.txt","目录/ä.TXT"));assert(!files.sameName("目录/甲.txt","目录/乙.txt"));
 Vfs v;v.add(files);assert(v.lockRead("/storage/user/目录/Ä.txt",11));assert(!v.canMutate("/storage/user/目录/ä.TXT",true));v.release(11);
 // A worker can replace native metadata while discovery retains a stable
 // published snapshot. Foreground I/O reports busy, without touching the SDK.
 files.background(working,nullptr);maintenance=true;allowLock=true;auto opens=fakeSdOpens;
 assert(take());files.captureMount(20000,16000);give();files.describe(d.to<JsonObject>());
 assert(d["physical_capacity_bytes"]=="10000"&&d["operation_pending"]==true);
 assert(!strcmp(files.read("/unrelated",0,bytes,4,n),"resource_busy")&&fakeSdOpens==opens);
 maintenance=false;files.publish();files.describe(d.to<JsonObject>());assert(d["physical_capacity_bytes"]=="20000");
 files.invalidate();files.describe(d.to<JsonObject>());assert(d["physical_capacity_bytes"].isNull());
 std::cout<<"SD status snapshots, SPI arbitration, post-mount permissions and I/O failures passed\n";
}
