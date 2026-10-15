#pragma once
#include "Core.h"
extern "C" {
#include "lua/lua.h"
}
namespace notesos {
inline constexpr size_t LuaSourceLimit=8192;
inline void runtimeContract(Core&core,JsonObject o){
 o["engine"]="lua-5.4.8";o["entry_source_limit_bytes"]=LuaSourceLimit;o["module_limit"]=8;o["native_driver_loading"]=false;o["memory_budget_semantics"]="elastic_limit_with_checked_actual_growth";
 o["validation"]["method"]="validate";o["validation"]["compile_only"]=true;o["validation"]["parser"]=LUA_RELEASE;o["native_call_depth_limit"]=LUAI_MAXCCALLS;
 auto budgets=o["budgets"].to<JsonObject>();budgets["memory_bytes"]["minimum"]=16384;budgets["memory_bytes"]["maximum"]=core.resources().limit();budgets["instruction_budget"]["minimum"]=1000;budgets["instruction_budget"]["maximum"]=200000;budgets["system_call_budget"]["minimum"]=1;budgets["system_call_budget"]["maximum"]=256;
}
}
