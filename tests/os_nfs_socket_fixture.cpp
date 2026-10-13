#include "NfsService.h"
#include <transports/SocketTransport.h>
#include <chrono>
#include <thread>
#include <iostream>
using namespace notesos;
int main(int argc,char**argv){
 if(argc!=2)return 2;MemoryFiles ram("ram","/storage/ram","ram");Vfs v;v.add(ram);
 const char*hello="Notes OS NFS bridge";size_t written=0;ram.write("/hello.txt",0,(const uint8_t*)hello,strlen(hello),false,written);
 esp32_nfs::SocketTransport tcp(0x7f000001);ResourceBudget budget;NfsService service(v,budget);NfsOptions o;strcpy(o.provider,"ram");o.wire.port=atoi(argv[1]);o.wire.readOnly=true;o.wire.bytesPerSecond=0;
 if(auto e=service.start(o,tcp,0x123456789)){std::cerr<<e<<std::endl;return 3;}
 std::cout<<"ready"<<std::endl;auto start=std::chrono::steady_clock::now();
 for(;;){auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();if(ms>15000)break;service.poll(ms);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}
