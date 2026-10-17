#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
namespace notesos {
// A read-only admission check. The SDK parser sees a request only after all
// declared bytes are available, so a fragmented peer never sleeps its OS loop.
struct HttpIngress {
 enum class State {Waiting,Ready,Rejected};
 struct Result{State state=State::Waiting;unsigned status=0;size_t headerBytes=0,bodyBytes=0;};
 static Result inspect(const char*bytes,size_t size,size_t available,size_t headerLimit,size_t bodyLimit){
  size_t end=0;for(size_t i=3;i<size;++i)if(!memcmp(bytes+i-3,"\r\n\r\n",4)){end=i+1;break;}
  if(!end)return size>=headerLimit?Result{State::Rejected,431}:Result{};
  size_t contentLength=0;bool supplied=false;size_t at=0;while(at+1<end&&!(bytes[at]=='\r'&&bytes[at+1]=='\n'))++at;at+=2;
  while(at+2<end){size_t next=at;while(next+1<end&&!(bytes[next]=='\r'&&bytes[next+1]=='\n'))++next;size_t colon=at;while(colon<next&&bytes[colon]!=':')++colon;if(colon==next)return {State::Rejected,400};
   size_t start=colon+1;while(start<next&&(bytes[start]==' '||bytes[start]=='\t'))++start;size_t stop=next;while(stop>start&&(bytes[stop-1]==' '||bytes[stop-1]=='\t'))--stop;
   auto named=[&](const char*name){return strlen(name)==colon-at&&!strncasecmp(bytes+at,name,colon-at);};
   if(named("Content-Length")){size_t value=0;if(start==stop)return {State::Rejected,400};for(size_t p=start;p<stop;++p){if(bytes[p]<'0'||bytes[p]>'9')return {State::Rejected,400};size_t digit=bytes[p]-'0';if(value>bodyLimit/10||(value==bodyLimit/10&&digit>bodyLimit%10))return {State::Rejected,413};value=value*10+digit;}if(supplied&&value!=contentLength)return {State::Rejected,400};contentLength=value;supplied=true;}
   else if(named("Content-Type")&&stop-start>=10&&!strncasecmp(bytes+start,"multipart/",10))return {State::Rejected,415};
   else if(named("Transfer-Encoding"))return {State::Rejected,501};
   else if(named("Expect"))return {State::Rejected,417};
   at=next+2;
  }
  if(available>end+bodyLimit)return {State::Rejected,413};
  return {available>=end+contentLength?State::Ready:State::Waiting,0,end,contentLength};
 }
};
}
