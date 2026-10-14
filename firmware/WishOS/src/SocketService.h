#pragma once
#include "Core.h"
#include "WireBytes.h"
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#ifdef ARDUINO
#include <lwip/sockets.h>
#include <lwip/inet.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
namespace notesos {
// Nonblocking IPv4 sockets. Data is explicitly read/written; events only announce readiness.
class SocketService {
 struct Socket{int fd=-1;uint32_t id=0;bool listener=false,connecting=false,notified=false;uint32_t owner=0;};
 Socket sockets_[4];uint32_t next_=1;Core&core_;bool registered_=false;
 void event(Socket&s,const char*name){JsonDocument d;d["id"]=s.id;d["owner"]=s.owner;core_.emit("/net/tcp",name,d.as<JsonObjectConst>(),s.owner);}
 Socket*find(uint32_t id){for(auto&s:sockets_)if(s.id==id&&id)return &s;return nullptr;}
 Socket*vacant(){for(auto&s:sockets_)if(s.fd<0)return &s;return nullptr;}
 static bool transient(){return errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR;}
 static bool nonblocking(int fd){return fcntl(fd,F_SETFL,O_NONBLOCK)==0;}
public:
 explicit SocketService(Core&c):core_(c){registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){((SocketService*)p)->releaseOwner(owner);});}
 ~SocketService(){core_.removeOwnerHook(this);closeAll();}
 void releaseOwner(uint32_t owner){for(auto&s:sockets_)if(s.fd>=0&&s.owner==owner){::close(s.fd);s={};}}
 bool active()const{for(auto&s:sockets_)if(s.fd>=0)return true;return false;}
 void closeAll(){for(auto&s:sockets_)if(s.fd>=0){::close(s.fd);s={};}}
 void poll(){for(auto&s:sockets_)if(s.fd>=0){
   if(s.connecting){fd_set write,error;FD_ZERO(&write);FD_ZERO(&error);FD_SET(s.fd,&write);FD_SET(s.fd,&error);timeval tv{};
     if(select(s.fd+1,nullptr,&write,&error,&tv)>0){int e=0;socklen_t n=sizeof e;getsockopt(s.fd,SOL_SOCKET,SO_ERROR,&e,&n);s.connecting=false;event(s,e?"failed":"connected");if(e){::close(s.fd);s={};}}continue;}
   if(s.listener){auto slot=vacant();if(!slot)continue;int fd=::accept(s.fd,nullptr,nullptr);if(fd<0)continue;if(!nonblocking(fd)){::close(fd);continue;}*slot={fd,next_++};slot->owner=s.owner;event(*slot,"accepted");continue;}
   uint8_t b;int n=recv(s.fd,&b,1,MSG_PEEK|MSG_DONTWAIT);
   if(n>0&&!s.notified){s.notified=true;event(s,"readable");}
   else if(!n||(n<0&&!transient())){event(s,"closed");::close(s.fd);s={};}
 }}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out){
   const char*fields=(!strcmp(m,"listen")||!strcmp(m,"connect"))?"address,port":!strcmp(m,"write")?"id,text,hex":!strcmp(m,"read")?"id,length":!strcmp(m,"close")?"id":"";
   for(auto f:a)if(!Core::containsMethod(fields,f.key().c_str()))return "unknown_socket_argument";
   for(auto key:{"id","length","port"})if(a.containsKey(key)&&!a[key].is<uint32_t>())return "socket_argument_type";
   if(a.containsKey("address")&&(!a["address"].is<const char*>()||a["address"].as<JsonString>().size()!=strlen(a["address"].as<const char*>())))return "socket_argument_type";

   if(!strcmp(m,"status")||(!strcmp(m,"read")&&!a["id"].is<uint32_t>())){out["active"]=active();auto list=out["sockets"].to<JsonArray>();for(auto&s:sockets_)if(s.fd>=0&&(!core_.callerOwner()||s.owner==core_.callerOwner())){auto o=list.add<JsonObject>();o["id"]=s.id;o["owner"]=s.owner;o["state"]=s.listener?"listening":s.connecting?"connecting":"connected";}out["capacity"]=4;out["address_family"]="ipv4";return nullptr;}
   if(!strcmp(m,"listen")||!strcmp(m,"connect")){
     if(!registered_)return "owner_hook_capacity";auto slot=vacant();if(!slot)return "socket_capacity";bool listen=!strcmp(m,"listen");
     uint32_t port=a["port"]|0u;const char*address=a["address"]|(listen?"0.0.0.0":"");sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(port);
     if(!port||port>65535||inet_pton(AF_INET,address,&addr.sin_addr)!=1)return "ipv4_address_and_port_required";
     int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return "socket_failed";
     if(!nonblocking(fd)){::close(fd);return "socket_failed";}
#ifdef SO_NOSIGPIPE
     int one=1;setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&one,sizeof one);
#endif
     if(listen){int one=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&one,sizeof one);if(::bind(fd,(sockaddr*)&addr,sizeof addr)||::listen(fd,2)){::close(fd);return "listen_failed";}}
     else if(::connect(fd,(sockaddr*)&addr,sizeof addr)&&errno!=EINPROGRESS){::close(fd);return "connect_failed";}
     *slot={fd,next_++,listen,!listen};slot->owner=core_.callerOwner();out["id"]=slot->id;out["state"]=listen?"listening":"connecting";return nullptr;
   }
   auto s=find(a["id"]|0u);if(!s)return "socket_not_found";if(core_.callerOwner()&&s->owner!=core_.callerOwner())return "socket_owned_by_other";
   if(!strcmp(m,"close")){::close(s->fd);*s={};return nullptr;}
   if(s->listener||s->connecting)return "socket_not_connected";
   if(!strcmp(m,"write")){uint8_t bytes[1024];size_t n=0;if(auto e=wirePayload(a,bytes,sizeof bytes,n))return e;
     int flags=MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
     flags|=MSG_NOSIGNAL;
#endif
     int wrote=send(s->fd,bytes,n,flags);if(wrote<0&&!transient())return "socket_write_failed";out["written"]=wrote<0?0:wrote;out["requested"]=n;return nullptr;
   }
   if(!strcmp(m,"read")){uint32_t limit=a["length"]|1024u;if(!limit||limit>1024)return "chunk_limit";uint8_t b[1024];int n=recv(s->fd,b,limit,MSG_DONTWAIT);if(n<0&&!transient())return "socket_read_failed";
     char hex[2049];const char*digits="0123456789abcdef";for(int i=0;i<n;++i){hex[i*2]=digits[b[i]>>4];hex[i*2+1]=digits[b[i]&15];}hex[n<0?0:n*2]=0;out["hex"]=hex;out["bytes"]=n<0?0:n;out["would_block"]=n<0;out["eof"]=n==0;s->notified=false;return nullptr;}
   return "unsupported_method";
 }
};
}
