#pragma once
#include "Core.h"
#include "WireBytes.h"
#include <ctype.h>
namespace notesos {
struct ProxySettings {
 char mode[16]="direct",host[254]{},username[128]{},password[128]{},bypass[256]="localhost,127.0.0.1";uint16_t port=1080;uint32_t timeout=10000;
 bool applies(const char*target)const{if(!strcmp(mode,"direct"))return false;char list[256];strcpy(list,bypass);char*save=nullptr;for(char*p=strtok_r(list,",",&save);p;p=strtok_r(nullptr,",",&save)){if(!strcasecmp(p,target))return false;if(*p=='.'){size_t a=strlen(target),b=strlen(p);if(a>b&&!strcasecmp(target+a-b,p))return false;}}return true;}
 void describe(JsonObject o)const{o["mode"] = JsonString(mode);o["host"] = JsonString(host);o["port"]=port;o["username"] = JsonString(username);o["password"] = JsonString(password);o["bypass"] = JsonString(bypass);o["timeout_ms"]=timeout;}
 static const char*merge(JsonObjectConst a,ProxySettings&n){for(auto f:a){const char*k=f.key().c_str();if(!Core::containsMethod("mode,host,port,username,password,bypass,timeout_ms",k))return "unknown_proxy_setting";
  if(!strcmp(k,"port")||!strcmp(k,"timeout_ms")){if(!f.value().is<uint32_t>())return "proxy_setting_type";uint32_t x=f.value();if(!strcmp(k,"port")){if(!x||x>65535)return "proxy_port";n.port=x;}else{if(x<100||x>120000)return "proxy_timeout";n.timeout=x;}}
  else{if(!f.value().is<const char*>())return "proxy_setting_type";const char*v=f.value();if(f.value().as<JsonString>().size()!=strlen(v)||strpbrk(v,"\r\n"))return "proxy_setting_type";char*p=!strcmp(k,"mode")?n.mode:!strcmp(k,"host")?n.host:!strcmp(k,"username")?n.username:!strcmp(k,"password")?n.password:n.bypass;size_t cap=!strcmp(k,"mode")?sizeof n.mode:!strcmp(k,"host")?sizeof n.host:!strcmp(k,"bypass")?sizeof n.bypass:128;if(!copy(p,cap,v))return "proxy_setting_limit";}
 }if(!Core::containsMethod("direct,http_connect,socks5",n.mode)||strcmp(n.mode,"direct")&&!*n.host||strchr(n.username,':'))return "proxy_configuration";return nullptr;}
};
// Incremental handshake: bounded buffers, no TLS parsing and no target-data
// over-read. Transport owners schedule step() and supply their own deadline.
class ProxyHandshake {
 ProxySettings config_;char target_[254]{};uint16_t port_=0;uint8_t tx_[800]{},rx_[1024]{};size_t size_=0,sent_=0,used_=0,need_=0;int phase_=0;const char*error_=nullptr;
 void connectRequest(){size_=0;tx_[size_++]=5;tx_[size_++]=1;tx_[size_++]=0;tx_[size_++]=3;size_t n=strlen(target_);tx_[size_++]=n;memcpy(tx_+size_,target_,n);size_+=n;tx_[size_++]=port_>>8;tx_[size_++]=port_;sent_=used_=0;need_=4;phase_=3;}
 void received(){if(phase_==1){if(rx_[0]!=5)return fail("socks_version");if(rx_[1]==0){connectRequest();return;}if(rx_[1]!=2||!*config_.username)return fail("socks_auth_method");size_=0;tx_[size_++]=1;size_t n=strlen(config_.username);tx_[size_++]=n;memcpy(tx_+size_,config_.username,n);size_+=n;n=strlen(config_.password);tx_[size_++]=n;memcpy(tx_+size_,config_.password,n);size_+=n;sent_=used_=0;need_=2;phase_=2;
 }else if(phase_==2){if(rx_[0]!=1||rx_[1])return fail("socks_auth_failed");connectRequest();}
 else if(phase_==3){if(rx_[0]!=5||rx_[1]||rx_[2])return fail("socks_connect_failed");if(rx_[3]==1)need_=10;else if(rx_[3]==4)need_=22;else if(rx_[3]==3)need_=5;else return fail("socks_address_type");phase_=4;}
 else if(phase_==4){if(rx_[3]==3&&used_==5){need_=7+rx_[4];if(need_>used_)return;}phase_=6;}
 }
 void fail(const char*e){error_=e;phase_=-1;}
public:
 const char*begin(const ProxySettings&c,const char*host,uint16_t port){config_=c;if(!copy(target_,sizeof target_,host)||!*host||strpbrk(host,"\r\n []"))return "proxy_target";port_=port;size_=sent_=used_=0;error_=nullptr;
 if(!strcmp(c.mode,"socks5")){tx_[0]=5;tx_[1]=*c.username?2:1;tx_[2]=0;tx_[3]=2;size_=*c.username?4:3;need_=2;phase_=1;}
 else{char auth[352]{};if(*c.username){char raw[256];snprintf(raw,sizeof raw,"%s:%s",c.username,c.password);const char*alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t n=strlen(raw),at=0;for(size_t i=0;i<n;i+=3){uint32_t v=uint8_t(raw[i])<<16;if(i+1<n)v|=uint8_t(raw[i+1])<<8;if(i+2<n)v|=uint8_t(raw[i+2]);auth[at++]=alphabet[v>>18];auth[at++]=alphabet[(v>>12)&63];auth[at++]=i+1<n?alphabet[(v>>6)&63]:'=';auth[at++]=i+2<n?alphabet[v&63]:'=';}auth[at]=0;}
 int n=snprintf((char*)tx_,sizeof tx_,"CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n%s%s%s\r\n",host,port,host,port,*auth?"Proxy-Authorization: Basic ":"",auth,*auth?"\r\n":"");if(n<0||n>=int(sizeof tx_))return "proxy_header_limit";size_=n;phase_=5;need_=0;}return nullptr;}
 // read/write return bytes, 0 for would-block, -1 for disconnected/error.
 template<class Io>void step(Io&io){if(phase_<=0||phase_==6)return;if(sent_<size_){int n=io.write(tx_+sent_,size_-sent_);if(n<0)return fail("proxy_transport");sent_+=n;if(sent_<size_)return;}
 for(unsigned work=0;work<256&&phase_>0&&phase_!=6;++work){int n=io.read(rx_+used_,1);if(n<0)return fail("proxy_transport");if(!n)return;++used_;
  if(phase_==5){if(used_>=4&&!memcmp(rx_+used_-4,"\r\n\r\n",4)){rx_[used_]=0;int status=0;if(sscanf((char*)rx_,"HTTP/%*u.%*u %d",&status)!=1||status/100!=2)fail("proxy_connect_rejected");else phase_=6;return;}if(used_>=sizeof rx_-1)return fail("proxy_header_limit");}
  else if(used_==need_){received();if(sent_<size_)return;}
 }}
 bool ready()const{return phase_==6;}const char*error()const{return error_;}
};
class NetworkProxy {
 mutable std::mutex mutex_;ProxySettings settings_;uint32_t revision_=1;bool valid_=true;
public:
 using Save=bool(*)(const char*,size_t);
 ProxySettings snapshot()const{std::lock_guard<std::mutex>lock(mutex_);return settings_;}
 bool restore(const char*text){JsonDocument d;ProxySettings n;if(deserializeJson(d,text)||ProxySettings::merge(d.as<JsonObjectConst>(),n)){valid_=false;return false;}std::lock_guard<std::mutex>lock(mutex_);settings_=n;valid_=true;return true;}
 const char*invoke(const char*m,JsonObjectConst a,JsonObject o,Save save=nullptr){if(auto e=argumentFields(a,!strcmp(m,"configure")?"settings,persist":""))return e;if(!strcmp(m,"configure")){for(auto f:a)if(!Core::containsMethod("settings,persist",f.key().c_str()))return "unknown_proxy_argument";if(!a["settings"].is<JsonObjectConst>()||a.containsKey("persist")&&!a["persist"].is<bool>())return "proxy_configuration";auto n=snapshot();if(auto e=ProxySettings::merge(a["settings"],n))return e;JsonDocument d;n.describe(d.to<JsonObject>());char text[1024];if(measureJson(d)>=sizeof text)return "proxy_setting_limit";size_t z=serializeJson(d,text,sizeof text);if((a["persist"]|false)&&(!save||!save(text,z)))return "persistence_failed";{std::lock_guard<std::mutex>lock(mutex_);settings_=n;++revision_;valid_=true;}}
 else if(strcmp(m,"read"))return "unsupported_method";snapshot().describe(o["configured"].to<JsonObject>());o["revision"]=revision_;o["restore_valid"]=valid_;return nullptr;}
 void describe(JsonObject o)const{o["scope"]="system_network";o["apply_time"]="new_connections";o["modes"]="direct,http_connect,socks5";o["dns"]="target_hostname_resolved_by_proxy";o["udp_tunneling"]=false;o["failure_fallback"]="none";o["raw_tcp_proxy_address"]="IPv4";auto s=o["settings"].to<JsonObject>();s["mode"]["enum"]="direct,http_connect,socks5";s["host"]["max_bytes"]=253;s["username"]["max_bytes"]=127;s["password"]["max_bytes"]=127;s["bypass"]["max_bytes"]=255;s["bypass"]["syntax"]="comma_separated_exact_hosts_or_dot_suffixes";s["port"]["minimum"]=1;s["port"]["maximum"]=65535;s["timeout_ms"]["minimum"]=100;s["timeout_ms"]["maximum"]=120000;}
};
}
