#pragma once
#include "SocketService.h"
namespace notesos {
class DatagramService {
 int fd_=-1,family_=AF_INET;bool notified_=false,registered_=false;uint32_t owner_=0;Core&core_;
 static bool address(const char*s,uint16_t port,int family,sockaddr_storage&out,socklen_t&len){out={};if(family==AF_INET){auto a=(sockaddr_in*)&out;a->sin_family=AF_INET;a->sin_port=htons(port);len=sizeof(*a);return inet_pton(family,s,&a->sin_addr)==1;}auto a=(sockaddr_in6*)&out;a->sin6_family=AF_INET6;a->sin6_port=htons(port);len=sizeof(*a);return inet_pton(family,s,&a->sin6_addr)==1;}
public:
 explicit DatagramService(Core&c):core_(c){registered_=core_.onOwnerRelease(this,[](void*p,uint32_t owner){auto&s=*(DatagramService*)p;if(s.owner_==owner)s.stop();});}~DatagramService(){core_.removeOwnerHook(this);stop();}
 bool active()const{return fd_>=0;}void stop(){if(fd_>=0)::close(fd_);fd_=-1;notified_=false;owner_=0;}
 void poll(){if(fd_<0||notified_)return;uint8_t b;int n=recv(fd_,&b,1,MSG_PEEK|MSG_DONTWAIT);if(n>=0){notified_=true;JsonDocument d;d["owner"]=owner_;core_.emit("/net/udp","readable",d.as<JsonObjectConst>(),owner_);}}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject out){
   if(strcmp(m,"read")&&active()&&core_.callerOwner()&&owner_!=core_.callerOwner())return "udp_owned_by_other";
   if(!strcmp(m,"close")){stop();return nullptr;}
   if(!strcmp(m,"open")){if(!registered_)return "owner_hook_capacity";if(active())return "udp_already_open";bool ipv6=a["ipv6"]|false;int family=ipv6?AF_INET6:AF_INET;uint32_t port=a["port"]|0u;if(!port||port>65535)return "port_range";
     sockaddr_storage addr;socklen_t len;if(!address(a["address"]|(ipv6?"::":"0.0.0.0"),port,family,addr,len))return "numeric_address_required";
     int fd=socket(family,SOCK_DGRAM,0);if(fd<0)return "socket_failed";if(fcntl(fd,F_SETFL,O_NONBLOCK)||bind(fd,(sockaddr*)&addr,len)){::close(fd);return "udp_bind_failed";}fd_=fd;family_=family;owner_=core_.callerOwner();return nullptr;}
   if(!strcmp(m,"read")){out["open"]=active();out["owner"]=owner_;out["ipv6"]=family_==AF_INET6;return nullptr;}if(!active())return "udp_not_open";
   if(!strcmp(m,"send")){uint32_t port=a["port"]|0u;sockaddr_storage addr;socklen_t len;if(!port||port>65535||!address(a["address"]|"",port,family_,addr,len))return "destination_required";
     const char*t=a["text"]|"";size_t n=strlen(t);uint8_t bytes[1024];
     if(a["hex"].is<const char*>()){const char*h=a["hex"];size_t z=strlen(h);if(z%2||z>2048)return "datagram_limit";n=z/2;for(size_t i=0;i<n;++i){if(!isxdigit((unsigned char)h[2*i])||!isxdigit((unsigned char)h[2*i+1]))return "invalid_hex";char pair[3]={h[2*i],h[2*i+1],0};bytes[i]=strtoul(pair,nullptr,16);}}
     else{if(n>1024)return "datagram_limit";memcpy(bytes,t,n);}int sent=sendto(fd_,bytes,n,MSG_DONTWAIT,(sockaddr*)&addr,len);if(sent<0)return "udp_send_failed";out["sent"]=sent;return nullptr;}
   if(!strcmp(m,"receive")){sockaddr_storage peer;socklen_t len=sizeof peer;uint8_t b[1025];int n=recvfrom(fd_,b,sizeof b,MSG_DONTWAIT,(sockaddr*)&peer,&len);notified_=false;if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK){out["available"]=false;return nullptr;}return "udp_read_failed";}if(n>1024)return "datagram_too_large";
     const char*digits="0123456789abcdef";char h[2049];for(int i=0;i<n;++i){h[2*i]=digits[b[i]>>4];h[2*i+1]=digits[b[i]&15];}h[2*n]=0;out["hex"]=h;out["available"]=true;
     char ip[64];auto v4=(sockaddr_in*)&peer;auto v6=(sockaddr_in6*)&peer;inet_ntop(family_,family_==AF_INET?(void*)&v4->sin_addr:(void*)&v6->sin6_addr,ip,sizeof ip);out["address"]=ip;out["port"]=ntohs(family_==AF_INET?v4->sin_port:v6->sin6_port);return nullptr;}
   return "unsupported_method";
 }
};
}
