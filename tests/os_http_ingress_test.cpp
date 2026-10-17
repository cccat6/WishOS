#include "HttpIngress.h"
#include <string>
#include <cassert>
#include <iostream>
using namespace notesos;
int main(){using State=HttpIngress::State;std::string header="POST /os/invoke HTTP/1.1\r\nHost: device\r\nContent-Length: 12\r\n\r\n";for(size_t n=0;n<header.size();++n)assert(HttpIngress::inspect(header.data(),n,n,1536,4096).state==State::Waiting);for(size_t n=0;n<12;++n)assert(HttpIngress::inspect(header.data(),header.size(),header.size()+n,1536,4096).state==State::Waiting);auto ready=HttpIngress::inspect(header.data(),header.size(),header.size()+12,1536,4096);assert(ready.state==State::Ready&&ready.bodyBytes==12);
 for(auto pair:{std::pair<const char*,unsigned>{"Content-Length: 4097",413},{"Content-Length: -1",400},{"Content-Length: 9\r\nContent-Length: 10",400},{"Content-Type: multipart/form-data; boundary=x",415},{"Transfer-Encoding: chunked",501},{"Expect: 100-continue",417}}){auto request="POST / HTTP/1.1\r\n"+std::string(pair.first)+"\r\n\r\n";assert(HttpIngress::inspect(request.data(),request.size(),request.size(),1536,4096).status==pair.second);}
 std::string get="GET / HTTP/1.1\r\nHost: device\r\n\r\n";assert(HttpIngress::inspect(get.data(),get.size(),get.size(),1536,4096).state==State::Ready);std::string huge(1536,'x');assert(HttpIngress::inspect(huge.data(),huge.size(),huge.size(),1536,4096).status==431);std::cout<<"Fragmented HTTP admission waits without consuming/dispatching, bounded lengths and complete requests passed\n";
}
