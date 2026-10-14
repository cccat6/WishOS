#include "drivers/esp32-services/EspWifiCapabilities.h"
#include "WifiManager.h"
#include <map>
#include <string>
#include <vector>
#include <cassert>
#include <iostream>
#include <cmath>
using namespace notesos;
struct Fake:WifiBackend {
 bool link=false,allowed=true,radio=false,reject=false;std::string current;int signalNow=-55,scan=-2;
 unsigned scans=0,applies=0;std::map<std::string,int>visible;std::vector<std::string>attempts;
 void defaults(WifiConfig&c)const override{EspWifiCapabilities::defaults(c);}
 const char*validate(const WifiConfig&c)const override{return EspWifiCapabilities::validate(c);}
 void describeConfiguration(JsonObject o)const override{EspWifiCapabilities::describe(o);}
 const char*apply(const WifiConfig&c)override{++applies;radio=c.enabled;return nullptr;}
 bool connected()const override{return link;}const char*ssid()override{return current.c_str();}int rssi()const override{return signalNow;}
 bool switchAllowed(bool force)const override{return force||allowed;}
 bool startScan()override{++scans;scan=-1;return true;}int scanState()override{return scan;}
 int signal(const char*s)override{return visible.count(s)?visible[s]:-128;}
 bool join(const WifiProfile&p)override{attempts.push_back(p.ssid);link=false;return !reject;}
 void disconnect(bool off)override{link=false;scan=-2;if(off)radio=false;}
 void done(){scan=visible.size();}void connectedTo(const char*s){current=s;link=true;}
};
static JsonDocument json(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static bool persistFail(const char*,size_t){return false;}
static void setup(WifiManager&m,Core&c,const char*extra="{}"){JsonDocument d,out;auto p=d["profiles"].to<JsonArray>();auto a=p.add<JsonObject>();a["id"]="low";a["ssid"]="Low";a["priority"]=1;auto b=p.add<JsonObject>();b["id"]="high";b["ssid"]="High";b["priority"]=10;auto s=json(extra);d["settings"]=s.as<JsonObjectConst>();c.clock(0);assert(!m.configure(d.as<JsonObjectConst>(),out.to<JsonObject>()));}
int main(){
 // Another adapter can have different RF/AP capabilities while reusing the
 // identical profile selection and manual intent mechanisms.
 struct Alternate:Fake {
  void defaults(WifiConfig&c)const override{c.powerDbm=-5;c.apChannel=36;c.apClients=8;strcpy(c.powerSave,"off");}
  const char*validate(const WifiConfig&c)const override{return c.powerDbm>=-10&&c.powerDbm<=30&&c.apChannel==36&&c.apClients<=8&&!strcmp(c.powerSave,"off")?nullptr:"adapter_setting";}
  void describeConfiguration(JsonObject o)const override{o["tx_power_dbm"]["minimum"]=-10;o["tx_power_dbm"]["maximum"]=30;o["ap_channel"]["choices"].to<JsonArray>().add(36);o["ap_max_clients"]["maximum"]=8;}
 };
 {Core kernel;Alternate native;WifiManager manager(native,kernel);JsonDocument reply;
  auto request=json(R"({"settings":{"tx_power_dbm":-3.1}})");assert(!manager.configure(request.as<JsonObjectConst>(),reply.to<JsonObject>()));
  manager.describe(reply.to<JsonObject>());assert(reply["settings"]["tx_power_dbm"]["minimum"]==-10&&reply["settings"]["ap_max_clients"]["maximum"]==8);
  JsonDocument saved;manager.config().write(saved.to<JsonObject>(),true);std::string bytes;serializeJson(saved,bytes);
  Alternate other;WifiManager restored(other,kernel);assert(restored.restore(bytes.c_str())&&std::abs(restored.config().powerDbm+3.1)<1e-6);
  request["settings"]["ap_channel"]=1;auto revision=manager.revision();assert(manager.configure(request.as<JsonObjectConst>(),reply.to<JsonObject>())&&manager.revision()==revision&&native.applies==0);
  Fake esp;WifiManager espManager(esp,kernel);assert(!espManager.restore(bytes.c_str())&&espManager.config().powerDbm==20&&esp.applies==0);
 }
 Core c;Fake h;WifiManager m(h,c);auto tick=[&](uint64_t t){c.clock(t);m.poll(t);};JsonDocument out;
 setup(m,c,R"({"scan_interval_ms":1000,"connect_timeout_ms":1000,"retry_backoff_ms":3000,"min_dwell_ms":0})");
 h.visible={{"Low",-40},{"High",-70}};tick(250);assert(h.scans==1);h.done();tick(251);assert(h.attempts.back()=="High");
 tick(1251);assert(!h.link);tick(1252);h.done();tick(1253);assert(h.attempts.back()=="Low");h.connectedTo("Low");tick(1254);
 // Default priority applies on loss, not preemptively to a healthy connection.
 tick(5000);assert(h.attempts.size()==2);
 auto settings=json(R"({"settings":{"switch_policy":"prefer_priority","candidate_confirmations":2}})");assert(!m.configure(settings.as<JsonObjectConst>(),out.to<JsonObject>()));
 tick(5300);h.done();tick(5301);assert(h.attempts.size()==2);tick(6301);h.done();tick(6302);assert(h.attempts.back()=="High");h.connectedTo("High");tick(6303);
 auto manual=json(R"({"hold":"until_resume","reconnect":true})");assert(!m.connect(*m.config().find("low"),manual.as<JsonObjectConst>(),6400));tick(6650);assert(h.attempts.back()=="Low");h.connectedTo("Low");tick(6651);
 unsigned before=h.attempts.size();m.requestScan();tick(8000);h.done();tick(8001);tick(10000);assert(h.attempts.size()==before);
 h.link=false;tick(11000);assert(h.attempts.back()=="Low");h.connectedTo("Low");tick(11001);
 // Explicit resume releases manual ownership; disabled automatic mode keeps the existing link.
 settings=json(R"({"settings":{"auto_connect":false}})");assert(!m.configure(settings.as<JsonObjectConst>(),out.to<JsonObject>()));m.resumeAuto(12000);tick(12250);h.link=false;before=h.scans;tick(13000);assert(h.scans==before);
 // Invalid/persistence-failed transactions keep the whole prior configuration.
 unsigned revision=m.revision();settings=json(R"({"settings":{"auto_connect":true,"connect_timeout_ms":0}})");assert(m.configure(settings.as<JsonObjectConst>(),out.to<JsonObject>()));assert(m.revision()==revision&&!m.config().autoConnect);
 settings=json(R"({"persist":true,"settings":{"auto_connect":true}})");assert(!strcmp(m.configure(settings.as<JsonObjectConst>(),out.to<JsonObject>(),persistFail),"persistence_failed"));assert(!m.config().autoConnect&&m.revision()==revision);
 JsonDocument saved;m.config().write(saved.to<JsonObject>(),true);std::string encoded;serializeJson(saved,encoded);Fake h2;Core c2;WifiManager restored(h2,c2);assert(restored.restore(encoded.c_str()));assert(!restored.config().autoConnect&&restored.config().count==2);
 saved["settings"]["retry_backoff_ms"]=0;encoded.clear();serializeJson(saved,encoded);assert(!restored.restore(encoded.c_str()));assert(restored.config().retryMs==3000);
 // Manual hold until loss may fall back to automatic, and timed holds expire explicitly.
 settings=json(R"({"settings":{"auto_connect":true}})");assert(!m.configure(settings.as<JsonObjectConst>(),out.to<JsonObject>()));manual=json(R"({"hold":"until_disconnect"})");assert(!m.connect(*m.config().find("low"),manual.as<JsonObjectConst>(),14000));tick(14250);h.connectedTo("Low");tick(14251);h.link=false;tick(15000);m.read(out.to<JsonObject>());assert(out["selection"]=="auto");
 // Runtime off survives background polling; resuming reuses policy rather than a hard-coded SSID.
 m.disconnect(true,false,16000);tick(16250);assert(!h.radio);before=h.scans;tick(17000);assert(h.scans==before);m.resumeAuto(18000);tick(18250);assert(h.radio);
 m.suspend();before=h.applies;tick(19000);assert(h.applies==before&&!h.radio);m.resume();tick(20000);assert(h.radio);
 // Editing unrelated policy does not cancel an association already in progress.
 {Core cc;Fake hh;WifiManager mm(hh,cc);setup(mm,cc);hh.visible={{"High",-40}};cc.clock(250);mm.poll(250);hh.done();cc.clock(251);mm.poll(251);auto update=json(R"({"settings":{"scan_interval_ms":2000}})");assert(!mm.configure(update.as<JsonObjectConst>(),out.to<JsonObject>()));cc.clock(501);mm.poll(501);mm.read(out.to<JsonObject>());assert(out["attempt_profile"]=="high"&&out["connection_pending"]==true&&hh.attempts.size()==1);}
 // Explicit RF settings still take effect after another system mode changed live hardware.
 {Core cc;Fake hh;WifiManager mm(hh,cc);cc.clock(0);mm.poll(0);unsigned applies=hh.applies;auto update=json(R"({"settings":{"power_save":"minimum"}})");assert(!mm.configure(update.as<JsonObjectConst>(),out.to<JsonObject>()));cc.clock(250);mm.poll(250);assert(hh.applies==applies+1);}
 // A live network lease blocks voluntary roaming, but never prevents recovery from link loss.
 {Core cc;Fake hh;WifiManager mm(hh,cc);auto t=[&](uint64_t at){cc.clock(at);mm.poll(at);};setup(mm,cc,R"({"scan_interval_ms":1000,"min_dwell_ms":0,"candidate_confirmations":1,"switch_policy":"prefer_priority"})");
 hh.visible={{"Low",-40}};t(250);hh.done();t(251);hh.connectedTo("Low");t(252);hh.visible["High"]=-30;hh.allowed=false;t(1251);hh.done();t(1252);assert(hh.attempts.size()==1&&hh.link);
 auto request=json(R"({"allow_disruption":false})");assert(!mm.connect(*mm.config().find("high"),request.as<JsonObjectConst>(),1300));t(1550);assert(hh.attempts.size()==1&&hh.link);hh.allowed=true;t(1551);assert(hh.attempts.back()=="High");}
 // Hidden networks can be tried; disabled or manual-only profiles are not automatic candidates.
 {Core cc;Fake hh;WifiManager mm(hh,cc);auto d=json(R"({"profiles":[{"id":"hidden","ssid":"Hidden","hidden":true,"priority":20},{"id":"disabled","ssid":"Disabled","enabled":false,"priority":100},{"id":"manual","ssid":"Manual","auto_connect":false,"priority":200}]})");assert(!mm.configure(d.as<JsonObjectConst>(),out.to<JsonObject>()));cc.clock(250);mm.poll(250);hh.done();mm.poll(251);assert(hh.attempts.back()=="Hidden");}
 // A timed manual choice does not become a permanent override.
 {Core cc;Fake hh;WifiManager mm(hh,cc);setup(mm,cc);auto request=json(R"({"hold":"timed","hold_ms":1000,"reconnect":false})");assert(!mm.connect(*mm.config().find("low"),request.as<JsonObjectConst>(),0));cc.clock(250);mm.poll(250);hh.connectedTo("Low");mm.poll(251);cc.clock(1000);mm.poll(1000);mm.read(out.to<JsonObject>());assert(out["selection"]=="auto");}
 // A hold deadline also ends a still-pending association rather than waiting for its long timeout.
 {Core cc;Fake hh;WifiManager mm(hh,cc);setup(mm,cc);auto request=json(R"({"hold":"timed","hold_ms":1000})");assert(!mm.connect(*mm.config().find("low"),request.as<JsonObjectConst>(),0));cc.clock(250);mm.poll(250);cc.clock(1000);mm.poll(1000);mm.read(out.to<JsonObject>());assert(out["selection"]=="auto"&&!out["connection_pending"].as<bool>());}
 // Default manual selection holds a healthy lower-priority link, then recovers automatically.
 {Core cc;Fake hh;WifiManager mm(hh,cc);auto t=[&](uint64_t at){cc.clock(at);mm.poll(at);};
 setup(mm,cc,R"({"switch_policy":"prefer_priority","scan_interval_ms":1000,"min_dwell_ms":0,"candidate_confirmations":1})");
 hh.visible={{"Low",-40},{"High",-70}};auto request=json("{}");assert(!mm.connect(*mm.config().find("low"),request.as<JsonObjectConst>(),0));
 t(250);hh.connectedTo("Low");t(251);mm.requestScan();t(1251);hh.done();t(1252);assert(hh.attempts.size()==1&&hh.current=="Low");
 mm.read(out.to<JsonObject>());assert(out["manual_hold"]=="until_disconnect");hh.link=false;t(1300);hh.done();t(1301);assert(hh.attempts.back()=="High");}
 // A failed initial manual association releases default ownership, without waiting out its cooldown.
 {Core cc;Fake hh;WifiManager mm(hh,cc);auto t=[&](uint64_t at){cc.clock(at);mm.poll(at);};setup(mm,cc,R"({"connect_timeout_ms":1000})");
 hh.visible={{"Low",-40},{"High",-70}};auto request=json("{}");assert(!mm.connect(*mm.config().find("low"),request.as<JsonObjectConst>(),0));t(250);t(1250);t(1251);hh.done();t(1252);assert(hh.attempts.size()==2&&hh.attempts.back()=="High");}
 // Default release never overrides a user's explicit global auto-connect disable.
 {Core cc;Fake hh;WifiManager mm(hh,cc);setup(mm,cc,R"({"auto_connect":false})");auto request=json("{}");assert(!mm.connect(*mm.config().find("low"),request.as<JsonObjectConst>(),0));cc.clock(250);mm.poll(250);hh.connectedTo("Low");mm.poll(251);hh.link=false;mm.poll(1000);mm.read(out.to<JsonObject>());assert(out["selection"]=="auto"&&hh.scans==0&&hh.attempts.size()==1);}
 // Persisted manual boot target has the same loss-release behavior as an ordinary manual choice.
 {Core cc;Fake hh;WifiManager mm(hh,cc);setup(mm,cc,R"({"selection":"manual","manual_profile":"low"})");JsonDocument snapshot;mm.config().write(snapshot.to<JsonObject>(),true);std::string value;serializeJson(snapshot,value);
 Fake h3;WifiManager boot(h3,cc);h3.visible={{"Low",-40},{"High",-70}};assert(boot.restore(value.c_str()));boot.poll(0);h3.connectedTo("Low");boot.poll(1);h3.link=false;boot.poll(2);h3.done();boot.poll(3);assert(h3.attempts.size()==2&&h3.attempts.back()=="High");}
 // Manual startup selection survives persistence, unlike one-off runtime overrides.
 {Core cc;Fake hh;WifiManager mm(hh,cc);setup(mm,cc,R"({"selection":"manual","manual_profile":"low","auto_connect":false})");JsonDocument snapshot;mm.config().write(snapshot.to<JsonObject>(),true);std::string value;serializeJson(snapshot,value);Fake h3;WifiManager boot(h3,cc);assert(boot.restore(value.c_str()));boot.poll(0);assert(h3.attempts.back()=="Low");}
 std::cout<<"Wi-Fi priority/failover/manual ownership/auto policy/atomic restore and sleep lifecycle passed\n";
}
