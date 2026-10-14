#include "drivers/esp32-services/LittleFsProvider.h"
#include <cassert>
#include <iostream>
using namespace notesos;
int main(){char root[]="/tmp/notes-fs-XXXXXX";assert(mkdtemp(root));fs::LittleFSFS native;bool mounted=false;LittleFsProvider p(native,mounted,root);FileInfo i;size_t n=0;uint8_t data[20]{};JsonDocument d;
 assert(!strcmp(p.stat("/",i),"provider_unavailable"));mounted=true;assert(!p.createFile("/test"));assert(!strcmp(p.createFile("/test"),"exists"));
 assert(!p.write("/test",0,(const uint8_t*)"hello",5,false,n)&&n==5);assert(!p.resize("/test",2));assert(!p.stat("/test",i)&&i.size==2);assert(!p.resize("/test",7));assert(!p.read("/test",0,data,sizeof data,n)&&n==7&&data[0]=='h'&&data[1]=='e'&&data[2]==0&&data[6]==0);
 assert(!p.read("/test",100,data,sizeof data,n)&&n==0);assert(!strcmp(p.read("/missing",0,data,sizeof data,n),"not_found"));assert(!p.mkdir("/目录"));assert(!p.rename("/test","/目录/字.txt"));assert(!p.flush("/目录/字.txt"));assert(!p.list("/目录",0,3,d.to<JsonObject>()));assert(d["entries"][0]["name"]=="字.txt");
 assert(!strcmp(p.remove("/目录"),"directory_not_empty"));assert(!p.remove("/目录/字.txt"));assert(!p.remove("/目录"));assert(!::rmdir(root));
 std::cout<<"LittleFS VFS bridge exclusive creation, resizing, sparse zero fill, EOF/errors, UTF-8 paths, flush and removal passed\n";
}
