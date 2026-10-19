#pragma once
#include "Core.h"
extern "C" {
#include "lua/lua.h"
}
namespace notesos {
inline constexpr size_t LuaSourceLimit=8192;
inline void runtimeContract(Core&core,JsonObject o){
 o["engine"]="lua-5.4.8";o["entry_source_limit_bytes"]=LuaSourceLimit;o["module_limit"]=8;o["native_driver_loading"]=false;o["memory_budget_semantics"]="elastic_limit_with_checked_actual_growth";
 o["libraries"]["input"]="/system/lib/input.lua";o["libraries"]["ai"]="/system/lib/ai.lua";
 o["validation"]["method"]="validate";o["validation"]["compile_only"]=true;o["validation"]["parser"]=LUA_RELEASE;o["native_call_depth_limit"]=LUAI_MAXCCALLS;
 auto cache=o["compile_cache"].to<JsonObject>();cache["storage"]="process_ram";cache["key"]="path_source_sha256_local_lua_abi";cache["source_key"]="provider_path_nonzero_content_revision";cache["source_access"]="current_provider_availability_and_read_lease";cache["budget"]="shared_source_and_compiled_lru";cache["configuration_scope"]="system";cache["apply_time"]="immediate";cache["configuration_fields"]="compile_cache_bytes,compile_cache_memory";cache["default_bytes"]=0;cache["memory_choices"]="auto,internal,external";
 auto budgets=o["budgets"].to<JsonObject>();budgets["memory_bytes"]["minimum"]=16384;budgets["memory_bytes"]["maximum"]=core.resources().limit();budgets["instruction_budget"]["minimum"]=1000;budgets["instruction_budget"]["maximum"]=200000;budgets["system_call_budget"]["minimum"]=1;budgets["system_call_budget"]["maximum"]=256;
}
}
