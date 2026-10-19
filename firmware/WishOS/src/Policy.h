#pragma once
#include "Core.h"
#include "BudgetJson.h"
#include <stdlib.h>
#include <stdio.h>
#include <utility>
#include <type_traits>
#include <math.h>
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
  mutable BudgetJson json_;
  JsonDocument saved_,defaults_;
  uint32_t revision_ = 1;
  const char*(*validator_)(const Policies&)=nullptr;
public:
  using Persist = bool (*)(const char *, size_t);
  Policies(const Setting *s, size_t n,ResourceBudget*memory=nullptr): fields_(s), count_(n),json_(memory),saved_(&json_),defaults_(&json_) { saved_.to<JsonObject>(); }
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
      if(!v.is<int64_t>()||v.as<double>()<s.minimum||v.as<double>()>s.maximum)return "integer_out_of_range";
      if(s.choices&&*s.choices){char text[24];snprintf(text,sizeof(text),"%lld",(long long)v.as<int64_t>());if(!choice(s.choices,text))return "unsupported_value";}return nullptr;
    }
    if(!strcmp(s.type,"number"))return v.is<double>()&&isfinite(v.as<double>())&&v.as<double>()>=s.minimum&&v.as<double>()<=s.maximum?nullptr:"number_out_of_range";
    if(!strcmp(s.type,"string"))return v.is<const char*>()&&strlen(v.as<const char*>())<=s.maximum?nullptr:"string_limit";
    if(!strcmp(s.type,"enum"))return v.is<const char*>()&&choice(s.choices,v.as<const char*>())?nullptr:"unsupported_value";
    return "unsupported_setting_type";
  }
  void resolve(const char *r,const char *scope,JsonObject out) const {
    out.clear();
    for(size_t i=0;i<count_;++i){const auto &s=fields_[i];if(strcmp(r,s.resource))continue;
      JsonDocument d(&json_);deserializeJson(d,s.initial);out[s.key]=d.as<JsonVariantConst>();
      auto baseline=defaults_["system"][r].as<JsonObjectConst>();if(baseline.containsKey(s.key))out[s.key]=baseline[s.key];
      auto sys=saved_["system"][r].as<JsonObjectConst>();if(sys.containsKey(s.key))out[s.key]=sys[s.key];
      if(scope&&*scope&&strcmp(scope,"system")){auto baseline=defaults_[scope][r].as<JsonObjectConst>();if(baseline.containsKey(s.key))out[s.key]=baseline[s.key];auto x=saved_[scope][r].as<JsonObjectConst>();if(x.containsKey(s.key))out[s.key]=x[s.key];}
    }
  }
  template<typename T>T get(const char *r,const char *key,T fallback,const char *scope="system") const {
    static_assert(std::is_arithmetic<T>::value,"Resolve string settings into a caller-owned JsonDocument");
    // Native polling must not allocate just to read an already validated value.
    bool present=false;JsonVariantConst value;
    auto layer=[&](JsonObjectConst object){if(object.containsKey(key)){value=object[key];present=true;}};
    layer(defaults_["system"][r]);layer(saved_["system"][r]);
    if(scope&&*scope&&strcmp(scope,"system")){layer(defaults_[scope][r]);layer(saved_[scope][r]);}
    if(present)return value.isNull()?fallback:value.as<T>();
    auto field=find(r,key);if(!field||!strcmp(field->initial,"null"))return fallback;
    if(!strcmp(field->initial,"true"))return T(true);if(!strcmp(field->initial,"false"))return T(false);
    if constexpr(std::is_floating_point<T>::value)return T(strtod(field->initial,nullptr));
    else if constexpr(std::is_unsigned<T>::value)return T(strtoull(field->initial,nullptr,10));
    else return T(strtoll(field->initial,nullptr,10));
  }
  size_t settingCount(const char*resource=nullptr)const{size_t n=0;for(size_t i=0;i<count_;++i)if(!resource||!strcmp(resource,fields_[i].resource))++n;return n;}
  void describe(JsonArray out,const char*resource=nullptr,unsigned start=0,unsigned limit=UINT32_MAX) const {
    unsigned seen=0;for(size_t i=0;i<count_;++i){const auto&s=fields_[i];if(resource&&strcmp(resource,s.resource))continue;if(seen++<start)continue;if(out.size()>=limit)break;auto o=out.add<JsonObject>();o["resource"]=s.resource;o["key"]=s.key;o["type"]=s.type;o["nullable"]=s.nullable;
      JsonDocument v(&json_);deserializeJson(v,s.initial);auto baseline=defaults_["system"][s.resource].as<JsonObjectConst>();o["default"]=baseline.containsKey(s.key)?baseline[s.key]:v.as<JsonVariantConst>();
      if(!strcmp(s.type,"integer")||!strcmp(s.type,"number")){o["minimum"]=s.minimum;o["maximum"]=s.maximum;}
      if(s.choices&&*s.choices)o["choices"]=s.choices;
      o["apply_time"]=s.applyTime;o["scope"]=s.systemOnly?"system":"system_or_application";
      o["inherit"]=true;
    }
  }
  void read(const char *scope,JsonObject out,const char*resource=nullptr,unsigned start=0,unsigned limit=UINT32_MAX) const {
    out["revision"]=revision_;out["scope"]=scope;auto overrides=out["overrides"].to<JsonObject>();auto sources=out["sources"].to<JsonObject>();auto effective=out["effective"].to<JsonObject>();unsigned seen=0,added=0;
    for(size_t i=0;i<count_;++i){auto r=fields_[i].resource;if(resource&&strcmp(resource,r))continue;bool earlier=false;for(size_t k=0;k<i;++k)if(!strcmp(fields_[k].resource,r)){earlier=true;break;}if(earlier)continue;
      if(seen++<start||added>=limit)continue;++added;resolve(r,scope,effective[r].to<JsonObject>());if(!saved_[scope][r].isNull())overrides[r]=saved_[scope][r];
      for(size_t k=0;k<count_;++k){auto&s=fields_[k];if(strcmp(s.resource,r))continue;sources[r][s.key]=saved_[scope][r].as<JsonObjectConst>().containsKey(s.key)?scope:saved_["system"][r].as<JsonObjectConst>().containsKey(s.key)?"system":"defaults";}
    }
    out["next_cursor"]=start+added;out["more"]=start+added<seen;
  }
  const char *apply(JsonObjectConst a,JsonObject out,bool dry=false,Persist persist=nullptr){
    for(auto field:a)if(!choice("scope,expected_revision,changes,persist",field.key().c_str()))return "unknown_policy_argument";
    if(a.containsKey("persist")&&!a["persist"].is<bool>())return "persist_boolean_required";
    if(!a["expected_revision"].isNull()&&(!a["expected_revision"].is<uint32_t>()||a["expected_revision"].as<uint32_t>()!=revision_))return "revision_conflict";
    if(!a["scope"].isNull()&&!a["scope"].is<const char*>())return "scope_string_required";
    const char *scope=a["scope"]|"system";if(!validScope(scope))return "invalid_scope";
    auto changes=a["changes"].as<JsonArrayConst>();if(changes.isNull()||!changes.size())return "changes_required";
    JsonDocument candidate(&json_);candidate.set(saved_);
    for(auto change:changes){if(!change.is<JsonObjectConst>())return "invalid_change";
      for(auto field:change.as<JsonObjectConst>())if(!choice("resource,settings,inherit",field.key().c_str()))return "unknown_policy_change_field";
      if(!change["resource"].is<const char*>())return "resource_string_required";
      const char *r=change["resource"]|"";
      if(change.containsKey("settings")&&!change["settings"].is<JsonObjectConst>())return "settings_required";
      if(change.containsKey("inherit")&&!change["inherit"].is<JsonArrayConst>())return "inherit_keys_required";
      auto values=change["settings"].as<JsonObjectConst>();auto inherited=change["inherit"].as<JsonArrayConst>();if(!values.size()&&!inherited.size())return "settings_required";
      for(auto key:inherited){if(!key.is<const char*>())return "inherit_key_string_required";const char*k=key.as<const char*>();auto s=find(r,k);if(!s)return "unknown_or_readonly_setting";
        if(s->systemOnly&&strcmp(scope,"system"))return "setting_requires_system_scope";
        if(values.containsKey(k))return "setting_and_inherit_conflict";
        candidate[scope][r].as<JsonObject>().remove(k);}
      for(auto p:values){auto s=find(r,p.key().c_str());if(!s)return "unknown_or_readonly_setting";
        if(s->systemOnly&&strcmp(scope,"system"))return "setting_requires_system_scope";
        if(auto e=check(*s,p.value()))return e;candidate[scope][r][p.key()]=p.value();}
      if(candidate[scope][r].is<JsonObject>()&&!candidate[scope][r].size())candidate[scope].as<JsonObject>().remove(r);
    }
    if(candidate[scope].is<JsonObject>()&&!candidate[scope].size())candidate.remove(scope);
    if(candidate.size()>8)return "scope_capacity";
    if(candidate.overflowed())return "memory_low";
    size_t n=measureJson(candidate);if(n>4096)return "policy_storage_limit";
    if(validator_){Policies view(fields_,count_,json_.resources());view.defaults_.set(defaults_);view.saved_.set(candidate);if(view.defaults_.overflowed()||view.saved_.overflowed())return "memory_low";if(auto e=validator_(view))return e;}
    if(!dry){
      if(a["persist"]|false){if(!persist)return "persistence_unavailable";char *text=(char*)malloc(n+1);if(!text)return "memory_low";
        serializeJson(candidate,text,n+1);bool ok=persist(text,n);free(text);if(!ok)return "persistence_failed";}
      saved_=std::move(candidate);++revision_;
    }
    out["revision"]=revision_;out["status"]=dry?"validated":"applied";out["apply_time"]="next_operation";return nullptr;
  }
  // Compiled device defaults are a baseline, separate from user overrides.
  // Loading a partial saved configuration keeps all unmentioned defaults live.
  bool defaults(const char*text){Policies candidate(fields_,count_,json_.resources());candidate.validator_=validator_;if(!candidate.restore(text))return false;
    if(validator_){Policies view(fields_,count_,json_.resources());view.defaults_.set(candidate.saved_);view.saved_.set(saved_);if(view.defaults_.overflowed()||view.saved_.overflowed()||validator_(view))return false;}
    JsonDocument baseline(&json_);baseline.set(candidate.saved_);if(baseline.overflowed())return false;swap(defaults_,baseline);return true;}
  bool restore(const char *text){
    if(!text||strlen(text)>4096)return false;JsonDocument d(&json_);if(deserializeJson(d,text)||!d.is<JsonObject>())return false;
    for(auto scope:d.as<JsonObjectConst>()){if(!validScope(scope.key().c_str())||!scope.value().is<JsonObjectConst>())return false;
      for(auto r:scope.value().as<JsonObjectConst>()){if(!r.value().is<JsonObjectConst>())return false;
        if(!r.value().as<JsonObjectConst>().size())return false;
        for(auto p:r.value().as<JsonObjectConst>()){auto s=find(r.key().c_str(),p.key().c_str());if(!s||check(*s,p.value())||(s->systemOnly&&strcmp(scope.key().c_str(),"system")))return false;}}}
    if(d.size()>8)return false;
    if(validator_){Policies view(fields_,count_,json_.resources());view.defaults_.set(defaults_);view.saved_.set(d);if(view.defaults_.overflowed()||view.saved_.overflowed())return false;if(validator_(view))return false;}
    saved_=std::move(d);return true;
  }
};
}
