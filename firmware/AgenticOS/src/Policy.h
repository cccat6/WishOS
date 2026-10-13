#pragma once
#include "Core.h"
#include <stdlib.h>
#include <stdio.h>
#include <utility>
#include <type_traits>
namespace notesos {
// Firmware capacity is a build limit. Values and scope overlays are runtime policy.
struct Setting {
  const char *resource, *key, *type, *initial, *choices;
  double minimum = 0, maximum = 0;
  bool nullable = false;
  bool systemOnly = false;
  const char *applyTime = "next_operation";
  constexpr Setting inSystem()const{auto s=*this;s.systemOnly=true;return s;}
  constexpr Setting at(const char*t)const{auto s=*this;s.applyTime=t;return s;}
};
class Policies {
  const Setting *fields_; size_t count_;
  JsonDocument saved_;
  uint32_t revision_ = 1;
  const char*(*validator_)(const Policies&)=nullptr;
public:
  using Persist = bool (*)(const char *, size_t);
  Policies(const Setting *s, size_t n): fields_(s), count_(n) { saved_.to<JsonObject>(); }
  uint32_t revision() const { return revision_; }
  void setValidator(const char*(*fn)(const Policies&)){validator_=fn;}
  static bool validScope(const char*s){return s&&*s&&strlen(s)<=48&&!strchr(s,'/');}
  const Setting *find(const char *r, const char *k) const {
    for(size_t i=0;i<count_;++i) if(!strcmp(r,fields_[i].resource)&&!strcmp(k,fields_[i].key))return &fields_[i];
    return nullptr;
  }
  static bool choice(const char *csv,const char *v) {
    if(!csv||!v)return false;
    for(const char *p=csv;*p;){const char *e=strchr(p,',');size_t n=e?size_t(e-p):strlen(p);
      if(strlen(v)==n&&!strncmp(p,v,n))return true;if(!e)break;p=e+1;}
    return false;
  }
  const char *check(const Setting &s, JsonVariantConst v) const {
    if(v.isNull())return s.nullable?nullptr:"null_not_supported";
    if(!strcmp(s.type,"boolean"))return v.is<bool>()?nullptr:"boolean_required";
    if(!strcmp(s.type,"integer")){
      if(!v.is<uint32_t>()||v.as<double>()<s.minimum||v.as<double>()>s.maximum)return "integer_out_of_range";
      if(s.choices&&*s.choices){char text[16];snprintf(text,sizeof(text),"%lu",(unsigned long)v.as<uint32_t>());if(!choice(s.choices,text))return "unsupported_value";}return nullptr;
    }
    if(!strcmp(s.type,"string"))return v.is<const char*>()&&strlen(v.as<const char*>())<=s.maximum?nullptr:"string_limit";
    if(!strcmp(s.type,"enum"))return v.is<const char*>()&&choice(s.choices,v.as<const char*>())?nullptr:"unsupported_value";
    return "unsupported_setting_type";
  }
  void resolve(const char *r,const char *scope,JsonObject out) const {
    out.clear();
    for(size_t i=0;i<count_;++i){const auto &s=fields_[i];if(strcmp(r,s.resource))continue;
      JsonDocument d;deserializeJson(d,s.initial);out[s.key]=d.as<JsonVariantConst>();
      auto sys=saved_["system"][r].as<JsonObjectConst>();if(sys.containsKey(s.key))out[s.key]=sys[s.key];
      if(scope&&*scope&&strcmp(scope,"system")){auto x=saved_[scope][r].as<JsonObjectConst>();if(x.containsKey(s.key))out[s.key]=x[s.key];}
    }
  }
  template<typename T>T get(const char *r,const char *key,T fallback,const char *scope="system") const {
    static_assert(!std::is_pointer<T>::value,"Resolve string settings into a caller-owned JsonDocument");
    JsonDocument d;resolve(r,scope,d.to<JsonObject>());return d[key].isNull()?fallback:d[key].as<T>();
  }
  void describe(JsonArray out) const {
    for(size_t i=0;i<count_;++i){auto o=out.add<JsonObject>();const auto &s=fields_[i];o["resource"]=s.resource;o["key"]=s.key;o["type"]=s.type;o["nullable"]=s.nullable;
      JsonDocument v;deserializeJson(v,s.initial);o["default"]=v.as<JsonVariantConst>();
      if(!strcmp(s.type,"integer")){o["minimum"]=s.minimum;o["maximum"]=s.maximum;}
      if(s.choices&&*s.choices)o["choices"]=s.choices;
      o["apply_time"]=s.applyTime;o["scope"]=s.systemOnly?"system":"system_or_application";
    }
  }
  void read(const char *scope,JsonObject out) const {
    out["revision"]=revision_;out["scope"]=scope;out["overrides"]=saved_[scope];
    auto sources=out["sources"].to<JsonObject>();
    for(size_t i=0;i<count_;++i){auto&s=fields_[i];sources[s.resource][s.key]=saved_[scope][s.resource].as<JsonObjectConst>().containsKey(s.key)?scope:saved_["system"][s.resource].as<JsonObjectConst>().containsKey(s.key)?"system":"defaults";}
    auto effective=out["effective"].to<JsonObject>();
    for(size_t i=0;i<count_;++i)if(!effective.containsKey(fields_[i].resource))resolve(fields_[i].resource,scope,effective[fields_[i].resource].to<JsonObject>());
  }
  const char *apply(JsonObjectConst a,JsonObject out,bool dry=false,Persist persist=nullptr){
    if(!a["expected_revision"].isNull()&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
    if(!a["scope"].isNull()&&!a["scope"].is<const char*>())return "scope_string_required";
    const char *scope=a["scope"]|"system";if(!validScope(scope))return "invalid_scope";
    if(!saved_.containsKey(scope)&&saved_.size()>=8)return "scope_capacity";
    auto changes=a["changes"].as<JsonArrayConst>();if(changes.isNull()||!changes.size())return "changes_required";
    JsonDocument candidate;candidate.set(saved_);
    for(auto change:changes){if(!change.is<JsonObjectConst>())return "invalid_change";
      const char *r=change["resource"]|"";auto values=change["settings"].as<JsonObjectConst>();if(!values.size())return "settings_required";
      for(auto p:values){auto s=find(r,p.key().c_str());if(!s)return "unknown_or_readonly_setting";
        if(s->systemOnly&&strcmp(scope,"system"))return "setting_requires_system_scope";
        if(auto e=check(*s,p.value()))return e;candidate[scope][r][p.key()]=p.value();}
    }
    if(candidate.overflowed())return "memory_low";
    size_t n=measureJson(candidate);if(n>4096)return "policy_storage_limit";
    if(validator_){Policies view(fields_,count_);view.saved_.set(candidate);if(view.saved_.overflowed())return "memory_low";if(auto e=validator_(view))return e;}
    if(!dry){
      if(a["persist"]|false){if(!persist)return "persistence_unavailable";char *text=(char*)malloc(n+1);if(!text)return "memory_low";
        serializeJson(candidate,text,n+1);bool ok=persist(text,n);free(text);if(!ok)return "persistence_failed";}
      saved_=std::move(candidate);++revision_;
    }
    out["revision"]=revision_;out["status"]=dry?"validated":"applied";out["apply_time"]="next_operation";return nullptr;
  }
  bool restore(const char *text){
    if(!text||strlen(text)>4096)return false;JsonDocument d;if(deserializeJson(d,text)||!d.is<JsonObject>())return false;
    for(auto scope:d.as<JsonObjectConst>()){if(!validScope(scope.key().c_str())||!scope.value().is<JsonObjectConst>())return false;
      for(auto r:scope.value().as<JsonObjectConst>()){if(!r.value().is<JsonObjectConst>())return false;
        if(!r.value().as<JsonObjectConst>().size())return false;
        for(auto p:r.value().as<JsonObjectConst>()){auto s=find(r.key().c_str(),p.key().c_str());if(!s||check(*s,p.value())||(s->systemOnly&&strcmp(scope.key().c_str(),"system")))return false;}}}
    if(d.size()>8)return false;
    if(validator_){Policies view(fields_,count_);view.saved_.set(d);if(view.saved_.overflowed())return false;if(validator_(view))return false;}
    saved_=std::move(d);return true;
  }
};
}
