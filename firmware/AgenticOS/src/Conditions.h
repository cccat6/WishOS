#pragma once
#include "Core.h"
#include <cmath>
namespace notesos {
// System-wide conditions over properties declared by any driver. No peripheral
// names, units, ADC rules or application actions belong in this evaluator.
class Conditions {
public:
  static constexpr unsigned CAPACITY=16;
private:
  struct Entry {
    char id[33]{},source[48]{},property[64]{},op[12]{},type[12]{};
    char target[128]{},baseline[128]{},candidate[128]{},error[64]{};
    double hysteresis=0;uint32_t holdMs=0,intervalMs=100;
    uint64_t due=0,pendingAt=0,lastAt=0;
    bool initialEvent=false,known=false,available=false,active=false,pending=false,candidateActive=false,sampled=false;
  } entries_[CAPACITY];
  Core&core_;
  static JsonVariantConst property(JsonVariantConst root,const char*path){
    char name[64];if(!copy(name,sizeof(name),path))return {};
    char*save=nullptr;for(char*part=strtok_r(name,".",&save);part;part=strtok_r(nullptr,".",&save)){
      if(root.is<JsonArrayConst>()){char*end;unsigned long index=strtoul(part,&end,10);if(!*part||*end||index>=root.size())return {};root=root[size_t(index)];}
      else root=root[part];
    }return root;
  }
  static bool typeMatches(const char*type,JsonVariantConst v){
    if(!strcmp(type,"boolean"))return v.is<bool>();
    if(!strcmp(type,"string"))return v.is<const char*>();
    return (!strcmp(type,"number")||!strcmp(type,"integer"))&&v.is<double>()&&std::isfinite(v.as<double>());
  }
  void emit(Entry&e,const char*event,JsonVariantConst value,bool initial,uint64_t now){
    JsonDocument d;d["id"]=e.id;d["source"]=e.source;d["property"]=e.property;d["value"]=value;d["initial"]=initial;d["at_ms"]=now;
    if(strcmp(e.op,"change"))d["active"]=e.active;
    core_.emit("/sys/conditions",event,d.as<JsonObjectConst>());
  }
  void observe(Entry&e,JsonVariantConst value,uint64_t now){
    if(!typeMatches(e.type,value)){e.available=e.pending=false;copy(e.error,sizeof(e.error),"sample_unavailable");return;}
    char encoded[128];if(measureJson(value)>=sizeof(encoded)){e.available=e.pending=false;copy(e.error,sizeof(e.error),"sample_limit");return;}
    serializeJson(value,encoded,sizeof(encoded));e.error[0]=0;
    if(e.sampled&&now-e.lastAt>(e.intervalMs*3u>1000u?e.intervalMs*3u:1000u))e.available=e.pending=false;
    e.lastAt=now;e.sampled=true;
    if(!strcmp(e.op,"change")){
      if(e.known){
        bool same=!strcmp(encoded,e.baseline);
        if(!same&&value.is<double>()){JsonDocument old;deserializeJson(old,e.baseline);same=std::abs(value.as<double>()-old.as<double>())<=e.hysteresis;}
        if(same){e.available=true;e.pending=false;return;}
      }
      if(!e.pending||strcmp(e.candidate,encoded)){copy(e.candidate,sizeof(e.candidate),encoded);e.pending=true;e.pendingAt=now;}
      if(now-e.pendingAt<e.holdMs)return;
      bool initial=!e.known;copy(e.baseline,sizeof(e.baseline),encoded);e.known=true;e.available=true;e.pending=false;
      if(!initial||e.initialEvent)emit(e,"changed",value,initial,now);return;
    }
    JsonDocument target;deserializeJson(target,e.target);bool desired;
    if(!strcmp(e.op,"equal"))desired=value==target.as<JsonVariantConst>();
    else {
      double v=value.as<double>(),threshold=target.as<double>();
      if(!strcmp(e.op,"above"))desired=v>=threshold-((e.known&&e.active)?e.hysteresis:0);
      else desired=v<=threshold+((e.known&&e.active)?e.hysteresis:0);
    }
    if(e.known&&e.available&&desired==e.active){e.pending=false;return;}
    if(!e.pending||e.candidateActive!=desired){e.pending=true;e.candidateActive=desired;e.pendingAt=now;}
    if(now-e.pendingAt<e.holdMs)return;
    bool initial=!e.known,changed=initial||desired!=e.active;e.known=true;e.available=true;e.active=desired;e.pending=false;
    if(changed&&(!initial||(desired&&e.initialEvent)))emit(e,desired?"entered":"exited",value,initial,now);
  }
public:
  explicit Conditions(Core&core):core_(core){}
  const char*set(JsonObjectConst a,JsonObject out,bool system=false){
    const char*id=a["id"]|"",*source=a["source"]|"",*field=a["property"]|"",*op=a["operator"]|"";
    if(!*id||strlen(id)>32)return "condition_id_limit";
    if(!system&&!strncmp(id,"system:",7))return "system_condition_use_policy";
    if(strcmp(op,"above")&&strcmp(op,"below")&&strcmp(op,"equal")&&strcmp(op,"change"))return "condition_operator";
    auto dev=core_.find(source);if(!dev||!core_.supports(*dev,"read"))return "condition_source";
    JsonDocument description;core_.describe(source,description.to<JsonObject>());
    auto spec=description["properties"][field];const char*type=spec["type"]|"";
    if(spec.isNull()||!strchr(spec["access"]|"",'r'))return "property_not_declared_readable";
    if(strcmp(type,"number")&&strcmp(type,"integer")&&strcmp(type,"boolean")&&strcmp(type,"string"))return "condition_property_type";
    bool numeric=!strcmp(type,"number")||!strcmp(type,"integer");
    if((!strcmp(op,"above")||!strcmp(op,"below"))&&!numeric)return "condition_requires_number";
    if(strcmp(op,"change")&&!typeMatches(type,a["value"]))return "condition_value_type";
    if(!a["hysteresis"].isNull()&&(!numeric||!a["hysteresis"].is<double>()||!std::isfinite(a["hysteresis"].as<double>())||a["hysteresis"].as<double>()<0))return "condition_hysteresis";
    for(const char*key:{"hold_ms","interval_ms"})if(!a[key].isNull()&&!a[key].is<uint32_t>())return "condition_timing_type";
    if(!a["emit_initial"].isNull()&&!a["emit_initial"].is<bool>())return "condition_boolean_type";
    Entry next;if(!copy(next.id,sizeof(next.id),id)||!copy(next.source,sizeof(next.source),source)||!copy(next.property,sizeof(next.property),field)||!copy(next.op,sizeof(next.op),op)||!copy(next.type,sizeof(next.type),type))return "condition_field_limit";
    next.holdMs=a["hold_ms"]|0u;next.intervalMs=a["interval_ms"]|100u;next.initialEvent=a["emit_initial"]|false;next.hysteresis=a["hysteresis"]|0.0;
    if(next.holdMs>3600000||next.intervalMs<20||next.intervalMs>60000)return "condition_timing_range";
    if(measureJson(a["value"])>=sizeof(next.target))return "condition_value_limit";serializeJson(a["value"],next.target,sizeof(next.target));
    Entry*slot=nullptr;for(auto&e:entries_)if(!strcmp(e.id,id)){slot=&e;break;}if(!slot)for(auto&e:entries_)if(!e.id[0]){slot=&e;break;}if(!slot)return "condition_capacity";
    *slot=next;out["id"]=id;out["status"]="armed";return nullptr;
  }
  const char*remove(const char*id){if(!strncmp(id,"system:",7))return "system_condition_use_policy";for(auto&e:entries_)if(e.id[0]&&!strcmp(e.id,id)){e={};return nullptr;}return "condition_not_found";}
  bool active(const char*id)const {for(auto&e:entries_)if(!strcmp(e.id,id))return e.known&&e.available&&e.active;return false;}
  void list(JsonObject out)const {
    out["capacity"]=CAPACITY;auto arr=out["conditions"].to<JsonArray>();
    for(auto&e:entries_)if(e.id[0]){auto o=arr.add<JsonObject>();o["id"]=e.id;o["source"]=e.source;o["property"]=e.property;o["operator"]=e.op;JsonDocument v;deserializeJson(v,e.target);if(strcmp(e.op,"change"))o["value"]=v.as<JsonVariantConst>();o["hysteresis"]=e.hysteresis;o["hold_ms"]=e.holdMs;o["interval_ms"]=e.intervalMs;o["emit_initial"]=e.initialEvent;if(e.known&&e.available)o["active"]=e.active;else o["active"]=nullptr;if(e.error[0])o["error"]=e.error;}
  }
  void tick(uint64_t now){
    bool processed[CAPACITY]{};
    for(unsigned i=0;i<CAPACITY;++i){auto&first=entries_[i];if(processed[i]||!first.id[0]||now<first.due)continue;
      JsonDocument args,state;const char*error=core_.invoke(first.source,"read",args.as<JsonObjectConst>(),state.to<JsonObject>());
      if(!error&&measureJson(state)>4096)error="observation_limit";
      for(unsigned j=i;j<CAPACITY;++j){auto&e=entries_[j];if(!e.id[0]||now<e.due||strcmp(e.source,first.source))continue;processed[j]=true;e.due=now+e.intervalMs;
        if(error){e.available=e.pending=false;copy(e.error,sizeof(e.error),error);}else observe(e,property(state.as<JsonVariantConst>(),e.property),now);
      }
    }
  }
};
}
