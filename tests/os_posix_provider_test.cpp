#include "drivers/esp32-services/PosixFileProvider.h"
#include <cassert>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace notesos;
int main(){char dir[]="/tmp/wishos-posix-XXXXXX";assert(mkdtemp(dir));bool mounted=true;std::recursive_mutex bus;PosixFileProvider p("test","/storage/test","sd",true,mounted,dir,"/dev/sd","fat32",512,&bus);size_t n=777;uint8_t bytes[8]{};
 std::thread owner([&]{bus.lock();assert(!strcmp(p.read("/x",0,bytes,8,n),"not_found"));bus.unlock();});owner.join();
 assert(n==0);assert(!p.mkdir("/子目录"));assert(!p.createFile("/子目录/a.txt"));assert(!p.write("/子目录/a.txt",0,(const uint8_t*)"abc",3,true,n)&&n==3);assert(!p.read("/子目录/a.txt",1,bytes,8,n)&&n==2&&!memcmp(bytes,"bc",2));
 // Cached readers cannot outlive an in-place write, rename/removal or remount.
 assert(!p.write("/子目录/a.txt",0,(const uint8_t*)"xyz",3,true,n));assert(!p.read("/子目录/a.txt",0,bytes,3,n)&&n==3&&!memcmp(bytes,"xyz",3));
 p.volume("fat32",512);assert(!p.read("/子目录/a.txt",0,bytes,3,n)&&n==3&&!memcmp(bytes,"xyz",3));
 // Interleaved readers retain independent offsets/identity, and a mutation
 // invalidates every cached descriptor before the path can be reused.
 assert(!p.write("/second",0,(const uint8_t*)"12345",5,true,n));
 assert(!p.read("/子目录/a.txt",1,bytes,2,n)&&n==2&&!memcmp(bytes,"yz",2));assert(!p.read("/second",2,bytes,2,n)&&n==2&&!memcmp(bytes,"34",2));assert(!p.read("/子目录/a.txt",0,bytes,3,n)&&!memcmp(bytes,"xyz",3));
 assert(!p.write("/second",0,(const uint8_t*)"new",3,true,n));assert(!p.read("/second",0,bytes,8,n)&&n==3&&!memcmp(bytes,"new",3));assert(!p.remove("/second"));assert(!strcmp(p.read("/second",0,bytes,8,n),"not_found"));
 // External replacement has no local write callback. New independent reads
 // on removable media must not reuse an old inode after rename/delete/recreate.
 p.removable=true;assert(!p.write("/external",0,(const uint8_t*)"old",3,true,n));assert(!p.read("/external",0,bytes,3,n)&&!memcmp(bytes,"old",3));auto revision=p.changeRevision();
 {std::ofstream replacement(std::string(dir)+"/incoming");replacement<<"new";}std::filesystem::rename(std::string(dir)+"/incoming",std::string(dir)+"/external");assert(p.changeRevision()==revision&&p.contentRevision()==0);
 assert(!p.read("/external",0,bytes,3,n));std::cerr<<"External rename read="<<std::string(reinterpret_cast<char*>(bytes),n)<<"\n";assert(n==3&&!memcmp(bytes,"new",3));
 assert(std::filesystem::remove(std::string(dir)+"/external"));assert(!strcmp(p.read("/external",0,bytes,3,n),"not_found")&&n==0);
 {std::ofstream recreated(std::string(dir)+"/external");recreated<<"end";}assert(!p.read("/external",0,bytes,3,n)&&n==3&&!memcmp(bytes,"end",3));assert(!p.remove("/external"));p.removable=false;
 // A reliable removable adapter may retain descriptors until its content
 // epoch changes. A new epoch invalidates even an atomic external replacement.
 {struct Reliable:PosixFileProvider {uint64_t epoch=100;using PosixFileProvider::PosixFileProvider;uint64_t contentRevision()const override{return epoch+changeRevision();}};
  Reliable reliable("epoch","/epoch","sd",true,mounted,dir,"/dev/sd","fat32",512,&bus);assert(!reliable.write("/versioned",0,(const uint8_t*)"one",3,true,n));assert(!reliable.read("/versioned",0,bytes,3,n)&&!memcmp(bytes,"one",3));
  {std::ofstream incoming(std::string(dir)+"/epoch-new");incoming<<"two";}std::filesystem::rename(std::string(dir)+"/epoch-new",std::string(dir)+"/versioned");++reliable.epoch;assert(!reliable.read("/versioned",0,bytes,3,n)&&n==3&&!memcmp(bytes,"two",3));assert(!reliable.remove("/versioned"));
 }
 char canonical[256];FileInfo canonicalInfo;assert(!p.canonicalPath("/子目录/a.txt",canonical,sizeof canonical,canonicalInfo));assert(!strcmp(canonical,"/子目录/a.txt")&&canonicalInfo.size==3);
 assert(!strcmp(p.createFile("/子目录/a.txt"),"exists"));p.background(true);assert(!strcmp(p.read("/子目录/a.txt",0,bytes,8,n),"resource_busy")&&n==0);p.background(false);
 p.writable=false;n=99;assert(!strcmp(p.write("/子目录/a.txt",0,bytes,8,true,n),"read_only")&&n==0);assert(!strcmp(p.remove("/子目录/a.txt"),"read_only"));p.writable=true;
 bus.lock();std::thread blocked([&]{size_t got=8;assert(!strcmp(p.read("/子目录/a.txt",0,bytes,8,got),"bus_busy")&&got==0);JsonDocument status;p.describe(status.to<JsonObject>());assert(status["filesystem"]=="fat32");});blocked.join();bus.unlock();
 p.captureSpace(8192,4096);p.writeQuota(4500);assert(!strcmp(p.write("/new",0,bytes,1,true,n),"storage_quota_exceeded"));FileInfo info;assert(!strcmp(p.stat("/new",info),"not_found"));p.writeQuota(0);
 // A conservative dirty free-space snapshot asks for explicit refresh;
 // it never claims the physical volume is full or scans during status/admission.
 p.captureSpace(8192,2048);p.writeQuota(8192);
 assert(!p.createFile("/space-test"));
 assert(!p.createFile("/space-test-2"));
 assert(!strcmp(p.createFile("/space-test-3"),"storage_space_refresh_required"));
 assert(!strcmp(p.stat("/space-test-3",info),"not_found"));
 p.captureSpace(8192,4096);assert(!p.createFile("/space-test-3"));
 p.writeQuota(0);assert(!p.remove("/space-test"));assert(!p.remove("/space-test-2"));assert(!p.remove("/space-test-3"));
 JsonDocument listing;assert(!p.list("/子目录",0,1,listing.to<JsonObject>()));assert(listing["entries"].size()==1);assert(!strcmp(p.rename("/missing","/new"),"not_found"));assert(!p.rename("/子目录/a.txt","/子目录/b.txt"));assert(!p.flush("/子目录/b.txt"));assert(!p.remove("/子目录/b.txt"));assert(!p.remove("/子目录"));mounted=false;assert(!strcmp(p.stat("/",info),"provider_unavailable"));std::filesystem::remove_all(dir);std::cout<<"POSIX providers preserve errors, names, exclusive creation, read-only policy, quotas and bus arbitration\n";
}
