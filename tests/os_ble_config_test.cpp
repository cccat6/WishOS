#include "BleConfiguration.h"
#include <cassert>
#include <string>
#include <iostream>
using namespace notesos;
static JsonDocument json(const char*s){JsonDocument d;assert(!deserializeJson(d,s));return d;}
static bool fail(const char*,size_t){return false;}
int main(){BleConfiguration cfg;JsonDocument o;auto a=json(R"({"auto_start":true,"profile":{"mode":"gatt","name":"Desk","service_uuid":"180a","characteristics":[{"uuid":"2a29","properties":2}]}})");assert(!cfg.configure(a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(cfg.autoStart());
 a=json(R"({"profile":{"name":"New name"}})");assert(!cfg.configure(a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(cfg.profile()["mode"]=="gatt"&&cfg.profile()["service_uuid"]=="180a");
 JsonDocument active;active.set(cfg.profile());assert(!cfg.differs(active.as<JsonObjectConst>()));
 a=json(R"({"persist":true,"profile":{"name":"Should not commit"}})");assert(cfg.configure(a.as<JsonObjectConst>(),o.to<JsonObject>(),fail));assert(cfg.profile()["name"]=="New name");
 a=json(R"({"auto_start":false,"profile":{"mode":"hid","report_map_hex":"xyz"}})");assert(cfg.configure(a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(cfg.autoStart()&&cfg.profile()["mode"]=="gatt");
 a=json(R"({"profile":{"mode":"central"}})");assert(!cfg.configure(a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(cfg.profile()["service_uuid"].isNull()&&cfg.differs(active.as<JsonObjectConst>()));
 cfg.read(o.to<JsonObject>());std::string saved;serializeJson(o["configured"],saved);BleConfiguration restored;assert(restored.restore(saved.c_str())&&restored.autoStart());assert(restored.profile()["mode"]=="central");
 unsigned profileRevision=restored.profileRevision();a=json(R"({"auto_start":false})");assert(!restored.configure(a.as<JsonObjectConst>(),o.to<JsonObject>(),nullptr));assert(restored.profileRevision()==profileRevision);
 assert(!restored.restore(R"({"auto_start":true,"profile":{"mode":3}})"));assert(restored.profile()["mode"]=="central");
 std::cout<<"BLE independent startup configuration, partial profile edits, atomic validation and persistence passed\n";
}
