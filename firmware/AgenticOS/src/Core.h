#pragma once
#include <ArduinoJson.h>
#include "ResourceBudget.h"
#include "AsyncContract.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
namespace notesos {
constexpr unsigned MAX_DEVICES = 48, MAX_RULES = 16, MAX_EVENTS = 32, MAX_TIMERS = 12;
constexpr size_t MAX_WIRE = 4096, MAX_EVENT = 512;
using Handler = const char *(*)(const char *, JsonObjectConst, JsonObject);
struct Device {
  const char *path, *methods, *guide;
  Handler handle;
  void (*describe)(JsonObject)=nullptr;
};
struct Event {
  uint32_t sequence = 0;
  uint64_t time = 0;
  uint32_t targetOwner = 0; // zero broadcasts; system observers can inspect all events
  char path[48]{}, name[40]{}, data[MAX_EVENT]{};
};
struct Rule {
  bool used = false, enabled = true, once = false, ran = false;
  uint32_t id = 0, cooldown = 0, owner = 0;
  uint64_t last = 0;
  char source[48]{}, event[40]{}, target[48]{}, method[32]{}, args[768]{}, where[256]{};
};
struct Timer {
  bool used = false;
  uint32_t id = 0, period = 0;
  uint32_t owner = 0;
  uint64_t due = 0;
};
inline bool copy(char *dst, size_t capacity, const char *src) {
  if (!src || strlen(src) >= capacity)
    return false;
  strcpy(dst, src);
  return true;
}
class Core {
  ResourceBudget resources_;
  Device devices_[MAX_DEVICES]{};
  unsigned count_ = 0;
  Event events_[MAX_EVENTS]{};
  Rule rules_[MAX_RULES]{};
  Timer timers_[MAX_TIMERS]{};
  uint32_t seq_ = 0, nextRule_ = 1, ruleOverruns_ = 0;
  uint64_t now_ = 0;
  uint32_t invocationOwner_=0,nextOwner_=1,nextTimerId_=1;
  struct OwnerHook{void*context=nullptr;void(*release)(void*,uint32_t)=nullptr;};OwnerHook ownerHooks_[16]{};
  int (*gate_)(const Event&)=nullptr;
  bool (*observe_)(const Event&)=nullptr;
  const char*(*invokeGate_)(const char*,const char*)=nullptr;
  Event deferred_[16]{};
  unsigned deferredCount_=0;uint32_t policyDrops_=0;

public:
  ResourceBudget&resources(){return resources_;}
  const char *replaceRules(JsonArrayConst definitions,const uint32_t*owned,size_t ownedCount,JsonArray ids){
    if(definitions.size()>MAX_RULES)return "rule_limit";
    for(auto definition:definitions)if(definition.as<JsonObjectConst>().containsKey("id"))return "replacement_rule_id_not_supported";
    Rule*backup=(Rule*)malloc(sizeof(rules_));if(!backup)return "memory_low";
    memcpy(backup,rules_,sizeof(rules_));uint32_t previousNext=nextRule_;
    // The UI service supplies its own trusted group IDs; app ownership must not
    // prevent replacement of the preceding system/default UI group.
    for(size_t i=0;i<ownedCount;++i)for(auto&r:rules_)if(r.used&&r.id==owned[i])r={};
    const char*error=nullptr;
    for(auto rule:definitions){JsonDocument result;if((error=setRule(rule.as<JsonObjectConst>(),result.to<JsonObject>())))break;if(!ids.add(result["id"])){error="memory_low";break;}}
    if(error){memcpy(rules_,backup,sizeof(rules_));nextRule_=previousNext;ids.clear();}
    free(backup);return error;
  }
  void clock(uint64_t now) {
    now_ = now;
  }
  uint64_t now() const {
    return now_;
  }
  bool add(Device d) {
    if (!d.path || !d.handle || count_ == MAX_DEVICES || find(d.path))
      return false;
    devices_[count_++] = d;
    return true;
  }
  const Device *find(const char *path) const {
    if (!path)
      return nullptr;
    for (unsigned i = 0; i < count_; i++)
      if (!strcmp(devices_[i].path, path))
        return &devices_[i];
    return nullptr;
  }
  bool supports(const Device &d, const char *method) const {
    if (!method || strchr(method, ','))
      return false;
    const char *p = d.methods;
    size_t n = strlen(method);
    while (*p) {
      const char *end = strchr(p, ',');
      size_t len = end ? size_t(end - p) : strlen(p);
      if (len == n && !strncmp(p, method, n))
        return true;
      if (!end)
        break;
      p = end + 1;
    }
    return false;
  }
  uint32_t acquireOwner(){return nextOwner_?nextOwner_++:0;}
  uint32_t callerOwner()const{return invocationOwner_;}
  bool onOwnerRelease(void*context,void(*fn)(void*,uint32_t)){for(auto&h:ownerHooks_)if(!h.release){h={context,fn};return true;}return false;}
  void removeOwnerHook(void*context){for(auto&h:ownerHooks_)if(h.context==context)h={};}
  void releaseOwner(uint32_t owner){if(!owner)return;for(auto&r:rules_)if(r.owner==owner)r={};for(auto&t:timers_)if(t.owner==owner)t={};for(auto h:ownerHooks_)if(h.release)h.release(h.context,owner);}
  const char *invoke(const char *path, const char *method, JsonObjectConst args, JsonObject out,uint32_t owner=0) {
    if(out.isNull())return "response_memory_low";
    const Device *d = find(path);
    if (!d)
      return "not_found";
    if (!supports(*d, method))
      return "unsupported_method";
    if(invokeGate_)if(auto e=invokeGate_(path,method))return e;
    auto previous=invocationOwner_;invocationOwner_=owner;auto result=d->handle(method,args,out);invocationOwner_=previous;return result;
  }
  unsigned deviceCount()const{return count_;}
  void list(JsonArray out,unsigned start=0,unsigned limit=MAX_DEVICES,bool details=true) const {
    for (unsigned i = start; i < count_ && i-start<limit; i++) {
      auto d = out.add<JsonObject>();
      d["path"] = devices_[i].path;
      d["version"] = 1;
      d["methods"] = devices_[i].methods;
      d["guide"] = devices_[i].guide;
      if(details&&devices_[i].describe)devices_[i].describe(d);
    }
  }
  const char*describe(const char*path,JsonObject out)const {
    auto d=find(path);if(!d)return "not_found";out["path"]=d->path;out["version"]=1;out["methods"]=d->methods;out["guide"]=d->guide;if(d->describe)d->describe(out);return nullptr;
  }
  bool emit(const char *path, const char *name, JsonObjectConst data, uint32_t targetOwner=0) {
    if (!path || !name || strlen(path) >= 48 || strlen(name) >= 40 ||
        measureJson(data) >= MAX_EVENT)
      return false;
    auto &e = events_[seq_ % MAX_EVENTS];
    e.sequence = ++seq_;
    e.time = now_;
    e.targetOwner = targetOwner;
    copy(e.path, sizeof(e.path), path);
    copy(e.name, sizeof(e.name), name);
    serializeJson(data, e.data, sizeof(e.data));
    return true;
  }
  uint32_t sequence() const {
    return seq_;
  }
  void events(uint32_t after, unsigned limit, JsonObject out) const {
    if (limit > 8)
      limit = 8;
    uint32_t oldest = seq_ >= MAX_EVENTS ? seq_ - MAX_EVENTS + 1 : 1;
    out["latest"] = seq_;
    out["rule_event_overruns"] = ruleOverruns_;
    out["oldest"] = oldest;
    out["gap"] = after < oldest - 1;
    auto a = out["events"].to<JsonArray>();
    uint32_t begin = after < oldest ? oldest : after + 1;
    unsigned n = 0;
    uint32_t cursor = after;
    for (uint32_t i = begin; i <= seq_ && n < limit; i++, n++) {
      auto &e = events_[(i - 1) % MAX_EVENTS];
      auto x = a.add<JsonObject>();
      x["sequence"] = e.sequence;
      x["at_ms"] = e.time;
      x["path"] = e.path;
      x["event"] = e.name;
      if(e.targetOwner)x["target_owner"]=e.targetOwner;
      JsonDocument d;
      deserializeJson(d, e.data);
      x["data"] = d.as<JsonObjectConst>();
      cursor = i;
    }
    out["cursor"] = cursor;
  }
  const char *setRule(JsonObjectConst a, JsonObject out) {
    const char *source = a["source"] | "", *event = a["event"] | "", *target = a["target"] | "",
               *method = a["method"] | "";
    const Device *d = find(target);
    if (!find(source) || !d || !supports(*d, method) || !*event)
      return "invalid_rule";
    uint32_t id = a["id"] | 0;
    Rule *r = nullptr;
    for (auto &x : rules_)
      if ((id && x.used && x.id == id) || (!id && !x.used)) {
        r = &x;
        break;
      }
    if (!r)
      return id ? "rule_not_found" : "rule_limit";
    if(r->used&&invocationOwner_&&r->owner!=invocationOwner_)return "rule_owned_by_other";
    Rule candidate;candidate.owner=invocationOwner_;
    if (!copy(candidate.source, sizeof(candidate.source), source) ||
        !copy(candidate.event, sizeof(candidate.event), event) ||
        !copy(candidate.target, sizeof(candidate.target), target) ||
        !copy(candidate.method, sizeof(candidate.method), method))
      return "rule_field_limit";
    auto args = a["args"].as<JsonObjectConst>();
    if (measureJson(args) >= sizeof(candidate.args))
      return "rule_args_limit";
    serializeJson(args, candidate.args, sizeof(candidate.args));
    auto where = a["where"].as<JsonObjectConst>();
    if (measureJson(where) >= sizeof(candidate.where))
      return "condition_limit";
    serializeJson(where, candidate.where, sizeof(candidate.where));
    for (JsonPairConst p : where) {
      if (p.value().is<JsonObjectConst>()) {
        auto condition = p.value().as<JsonObjectConst>();
        if (!condition.size())
          return "empty_condition";
        for (JsonPairConst op : condition) {
          const char *k = op.key().c_str();
          if ((strcmp(k, "lt") && strcmp(k, "lte") && strcmp(k, "gt") && strcmp(k, "gte")) ||
              !op.value().is<double>())
            return "invalid_numeric_condition";
        }
      }
    }
    if (!a["cooldown_ms"].isNull() && !a["cooldown_ms"].is<uint32_t>())
      return "invalid_cooldown";
    candidate.used = true;
    candidate.id = id ? id : nextRule_++;
    candidate.cooldown = a["cooldown_ms"] | 1000;
    candidate.once = a["once"] | false;
    *r = candidate;
    out["id"] = r->id;
    return nullptr;
  }
  void listRules(JsonArray out) const {
    for (auto &r : rules_)
      if (r.used) {
        auto x = out.add<JsonObject>();
        x["id"] = r.id;
        x["enabled"] = r.enabled;x["owner"]=r.owner;
        x["source"] = r.source;
        x["event"] = r.event;
        x["target"] = r.target;
        x["method"] = r.method;
        x["cooldown_ms"] = r.cooldown;
        x["once"] = r.once;
        JsonDocument a;
        deserializeJson(a, r.args);
        x["args"] = a.as<JsonObjectConst>();
        JsonDocument w;
        deserializeJson(w, r.where);
        x["where"] = w.as<JsonObjectConst>();
      }
  }
  bool removeRule(uint32_t id) {
    for (auto &r : rules_)
      if (r.used && r.id == id) {
        if(invocationOwner_&&r.owner!=invocationOwner_)return false;
        r.used = false;
        return true;
      }
    return false;
  }
  // Evaluate a snapshot only once. Rule-result events cannot recursively execute in this tick.
  void setInvocationGate(const char*(*fn)(const char*,const char*)){invokeGate_=fn;}
  void setEventGate(int(*gate)(const Event&)){gate_=gate;}
  void setEventObserver(bool(*fn)(const Event&)){observe_=fn;}
  uint32_t policyDrops()const{return policyDrops_;}
  unsigned deferredCount()const{return deferredCount_;}
  void dispatchOne(const Event&e){
    // Rule loading/removal is a legitimate action. New rules start with the next
    // event; removed rules must not run later in this dispatch.
    uint32_t candidates[MAX_RULES];unsigned count=0;
    for(auto&r:rules_)if(r.used&&r.enabled)candidates[count++]=r.id;
    if(observe_&&observe_(e))return;
    for(unsigned i=0;i<count;++i)for(auto&r:rules_)if(r.id==candidates[i])
        if (r.used && r.enabled && (!e.targetOwner||!r.owner||r.owner==e.targetOwner) && !strcmp(e.path, r.source) && !strcmp(e.name, r.event)) {
          JsonDocument data, condition;
          deserializeJson(data, e.data);
          deserializeJson(condition, r.where);
          bool matches = true;
          for (JsonPairConst p : condition.as<JsonObjectConst>()) {
            if (p.value().is<JsonObjectConst>()) {
              auto cond = p.value().as<JsonObjectConst>();
              auto actual = data[p.key()];
              if (!actual.is<double>()) {
                matches = false;
                break;
              }
              double v = actual.as<double>();
              if ((!cond["lt"].isNull() && !(v < cond["lt"].as<double>())) ||
                  (!cond["gt"].isNull() && !(v > cond["gt"].as<double>())) ||
                  (!cond["lte"].isNull() && !(v <= cond["lte"].as<double>())) ||
                  (!cond["gte"].isNull() && !(v >= cond["gte"].as<double>()))) {
                matches = false;
                break;
              }
            } else if (data[p.key()] != p.value()) {
              matches = false;
              break;
            }
          }
          if (!matches)
            continue;
          if (r.ran && now_ - r.last < r.cooldown)
            continue;
          r.last = now_;
          r.ran = true;
          if (r.once)
            r.enabled = false;
          JsonDocument a, result;
          deserializeJson(a, r.args);
          auto o = result.to<JsonObject>();
          auto executedId=r.id;
          const char *error = invoke(r.target, r.method, a.as<JsonObjectConst>(), o,r.owner);
          o["rule_id"] = executedId;
          o["source_sequence"] = e.sequence;
          if (error)
            o["error"] = error;
          emit("/sys/rules", "executed", o);
        }
  }
  void dispatch(uint32_t &cursor) {
    unsigned remaining=0;
    for(unsigned i=0;i<deferredCount_;++i){Event e=deferred_[i];int g=gate_?gate_(e):0;
      if(g==1)deferred_[remaining++]=e;else if(g==2)++policyDrops_;else dispatchOne(e);}
    deferredCount_=remaining;
    const uint32_t end=seq_, oldest=end>=MAX_EVENTS?end-MAX_EVENTS+1:1;
    uint32_t begin=cursor<oldest?oldest:cursor+1;
    if(cursor+1<oldest)ruleOverruns_+=oldest-cursor-1;
    for(uint32_t i=begin;i<=end;++i){Event e=events_[(i-1)%MAX_EVENTS];if(e.sequence!=i){++ruleOverruns_;continue;}
      int g=gate_?gate_(e):0;
      if(g==1){if(deferredCount_<16)deferred_[deferredCount_++]=e;else ++policyDrops_;}
      else if(g==2)++policyDrops_;else dispatchOne(e);
    }
    cursor=end;
  }
  const char *timer(JsonObjectConst a, JsonObject out) {
    if ((!a["id"].isNull()&&!a["id"].is<uint32_t>()) || !a["delay_ms"].is<uint32_t>())
      return "invalid_timer";
    if (!a["period_ms"].isNull() && !a["period_ms"].is<uint32_t>())
      return "invalid_timer";
    uint32_t id=a["id"]|0u,delay=a["delay_ms"],period=a["period_ms"]|0;
    if(a["id"].isNull()){
      for(unsigned tries=0;tries<=MAX_TIMERS;++tries){if(!nextTimerId_)return "timer_id_exhausted";id=nextTimerId_++;bool used=false;for(auto&t:timers_)used|=t.used&&t.id==id;if(!used)break;}
    }
    if (!id || !delay || (period && period < 10))
      return "invalid_timer";
    Timer *t = nullptr;
    for (auto &x : timers_)
      if (x.used && x.id == id) {
        t = &x;
        break;
      }
    if (!t)
      for (auto &x : timers_)
        if (!x.used) {
          t = &x;
          break;
        }
    if (!t)
      return "timer_limit";
    if(t->used&&invocationOwner_&&t->owner!=invocationOwner_)return "timer_owned_by_other";
    *t = {true, id, period, invocationOwner_, now_ + delay};
    out["id"] = id;
    out["due_ms"] = t->due;
    return nullptr;
  }
  void listTimers(JsonArray out) const {
    for (auto &t : timers_)
      if (t.used) {
        auto x = out.add<JsonObject>();
        x["id"] = t.id;
        x["due_ms"] = t.due;
        x["period_ms"] = t.period;x["owner"]=t.owner;
      }
  }
  bool removeTimer(uint32_t id) {
    for (auto &t : timers_)
      if (t.used && t.id == id) {
        if(invocationOwner_&&t.owner!=invocationOwner_)return false;
        t.used = false;
        return true;
      }
    return false;
  }
  void tick() {
    for (auto &t : timers_)
      if (t.used && now_ >= t.due) {
        JsonDocument d;
        auto o = d.to<JsonObject>();
        o["id"] = t.id;o["owner"]=t.owner;
        o["scheduled_ms"] = t.due;
        emit("/sys/timers", "fired", o,t.owner);
        if (t.period)
          t.due += ((now_ - t.due) / t.period + 1) * t.period;
        else
          t.used = false;
      }
  }
  uint64_t nextTimer() const {
    uint64_t n = 0;
    for (auto &t : timers_)
      if (t.used && (!n || t.due < n))
        n = t.due;
    return n;
  }
};
} // namespace notesos
