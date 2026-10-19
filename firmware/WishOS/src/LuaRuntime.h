#pragma once
#include "Core.h"
#include "Vfs.h"
#include "RuntimeCallbacks.h"
#include "RuntimeCallbacksBytecode.h"
#include "RuntimeCompileCache.h"
static_assert(notesos::CALLBACK_BYTECODE_SOURCE_HASH==notesos::callbackSourceHash(),"Regenerate the runtime callback library");
#include <cmath>
#include <stddef.h>
#include <stdlib.h>
#include <new>
#include <utility>
extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
#include "lua/lualib.h"
}
namespace notesos {
class LuaRuntime {
public:
  using Resize=void*(*)(void*,void*,size_t);
  using Reader=const char*(*)(void*,const char*,char*&,size_t&);
private:
  ArduinoJson::Allocator*jsonAllocator_=nullptr;
  JsonDocument*document(){return jsonAllocator_?new(std::nothrow)JsonDocument(jsonAllocator_):new(std::nothrow)JsonDocument;}
  Resize resize_=nullptr;void*allocatorContext_=nullptr;bool ownerReleased_=false;
  void releaseOwned(){if(!isolated_&&!ownerReleased_){ownerReleased_=true;clearCallbacks();core_.releaseOwner(owner_);}}
  Reader reader_=nullptr;void*readerContext_=nullptr;char base_[256]="/",chunk_[256]="application";RuntimeCompileCache*compileCache_=nullptr;
  int compileChunk(lua_State*L,const char*source,size_t size,const char*path){return compileCache_?compileCache_->load(L,path,source,size):luaL_loadbufferx(L,source,size,path,"t");}
  struct Module {char path[256]{};bool ready=false;int ref=LUA_NOREF;};
  Module*modules_=nullptr;unsigned moduleCount_=0,moduleCapacity_=0;
  uint32_t reservation_=0,owner_=0;bool stopped_=false,isolated_=false;
  void(*yield_)(void*)=nullptr;bool(*cancelled_)(void*)=nullptr;void*executionContext_=nullptr;
  JsonVariantConst computeInput_;JsonVariant computeOutput_;
  struct alignas(max_align_t) Block {size_t size;};
  Core &core_;lua_State*state_=nullptr;size_t used_=0,peak_=0,budget_;
  uint32_t steps_=0,stepLimit_,calls_=0,callLimit_;bool loading_=false,preparing_=false,executing_=false,fault_=false,consumed_=false;
  const char*source_=nullptr;size_t sourceSize_=0;const Event*event_=nullptr;int operation_=0;
  struct Subscription{char path[48]{};bool manual=false,callbacks=false;uint32_t manualAfter=0;};
  Subscription*subscriptions_=nullptr;unsigned subscriptionCount_=0,subscriptionCapacity_=0,callbackCount_=0;int callbacksRef_=LUA_NOREF;bool callbacksPending_=false,legacyEvent_=false;char error_[160]{};
  char*scratch_=nullptr;JsonDocument*input_=nullptr,*output_=nullptr;size_t jsonBudget_=0;
  static LuaRuntime& self(lua_State*L){return **(LuaRuntime**)lua_getextraspace(L);}
  static void*allocate(void*context,void*ptr,size_t,size_t n){
    auto&s=*(LuaRuntime*)context;Block*old=ptr?((Block*)ptr-1):nullptr;size_t previous=old?old->size:0;
    if(!n){if(old){s.used_-=previous;if(s.resize_){s.resize_(s.allocatorContext_,old,0);s.core_.resources().used(s.reservation_,s.used_);}else s.core_.resources().resize(s.reservation_,old,0,previous);}return nullptr;}
    if(n>SIZE_MAX-sizeof(Block))return nullptr;size_t bytes=n+sizeof(Block);
    if(bytes>s.budget_-s.used_+previous)return nullptr;
    auto next=(Block*)(s.resize_?s.resize_(s.allocatorContext_,old,bytes):s.core_.resources().resize(s.reservation_,old,bytes,previous));if(!next)return nullptr;next->size=bytes;s.used_=s.used_-previous+bytes;if(s.resize_)s.core_.resources().used(s.reservation_,s.used_);if(s.used_>s.peak_)s.peak_=s.used_;return next+1;
  }
  static void hook(lua_State*L,lua_Debug*){auto&s=self(L);s.steps_+=lua_gethookcount(L);if(s.cancelled_&&s.cancelled_(s.executionContext_))luaL_error(L,"cancelled");if(s.yield_)s.yield_(s.executionContext_);if(s.steps_>=s.stepLimit_){lua_sethook(L,hook,LUA_MASKCOUNT,1);lua_yield(L,0);}}
  static int continuation(lua_State*L,int,lua_KContext results){auto&s=self(L);if(s.operation_==4){s.jsonBudget_=0;if(!fromLua(L,-1,s.computeOutput_))return luaL_error(L,"compute_result_limit");}return int(results);}
  void cleanup(){delete input_;delete output_;input_=output_=nullptr;free(scratch_);scratch_=nullptr;}
  #include "LuaBindings.inc"
  static bool fromLua(lua_State*L,int index,JsonVariant out,unsigned depth=0){
    auto&s=self(L);if(depth>8||s.jsonBudget_>4096)return false;index=lua_absindex(L,index);s.jsonBudget_+=16;if(s.jsonBudget_>4096)return false;
    switch(lua_type(L,index)){
    case LUA_TLIGHTUSERDATA:if(lua_touserdata(L,index)!=&nullTag_)return false;return out.set(nullptr);
    case LUA_TNIL:return out.set(nullptr);
    case LUA_TBOOLEAN:return out.set(bool(lua_toboolean(L,index)));
    case LUA_TNUMBER:if(lua_isinteger(L,index))return out.set(int64_t(lua_tointeger(L,index)));else{double n=lua_tonumber(L,index);if(!std::isfinite(n))return false;return out.set(n);}
    case LUA_TSTRING:{size_t n;const char*p=lua_tolstring(L,index,&n);if(n>4096-s.jsonBudget_)return false;s.jsonBudget_+=n;return out.set(JsonString(p,n));}
    case LUA_TTABLE:{bool array=lua_rawlen(L,index)>0||isArray(L,index);unsigned count=0;if(array){if(out.to<JsonArray>().isNull())return false;}else if(out.to<JsonObject>().isNull())return false;
      lua_pushnil(L);while(lua_next(L,index)){
        if(++count>128){lua_pop(L,2);return false;}bool ok=false;
        if(array&&lua_isinteger(L,-2)){lua_Integer n=lua_tointeger(L,-2);if(n>=1&&n<=128)ok=fromLua(L,-1,out.as<JsonArray>()[size_t(n-1)].to<JsonVariant>(),depth+1);}
        else if(!array&&lua_type(L,-2)==LUA_TSTRING){size_t keySize;const char*k=lua_tolstring(L,-2,&keySize);s.jsonBudget_+=keySize;if(keySize<=128&&strlen(k)==keySize)ok=fromLua(L,-1,out[k].to<JsonVariant>(),depth+1);}
        lua_pop(L,1);if(!ok){lua_pop(L,1);return false;}}
      return true;}
    default:return false;
    }
  }
  static void toLua(lua_State*L,JsonVariantConst v,unsigned depth=0){
    if(!lua_checkstack(L,8)){luaL_error(L,"conversion_stack_limit");return;}
    if(depth>10){luaL_error(L,"result_nesting_limit");return;}
    if(v.is<JsonObjectConst>()){lua_newtable(L);for(auto p:v.as<JsonObjectConst>()){toLua(L,p.value(),depth+1);lua_setfield(L,-2,p.key().c_str());}}
    else if(v.is<JsonArrayConst>()){lua_newtable(L);lua_rawgetp(L,LUA_REGISTRYINDEX,&arrayTag_);lua_setmetatable(L,-2);lua_Integer i=1;for(auto x:v.as<JsonArrayConst>()){toLua(L,x,depth+1);lua_rawseti(L,-2,i++);}}
    else if(v.is<const char*>()){auto text=v.as<JsonString>();lua_pushlstring(L,text.c_str(),text.size());}
    else if(v.is<bool>())lua_pushboolean(L,v.as<bool>());
    else if(v.is<int64_t>())lua_pushinteger(L,v.as<int64_t>());
    else if(v.is<double>())lua_pushnumber(L,v.as<double>());
    else lua_pushlightuserdata(L,&nullTag_);
  }
  static int invokeCommon(lua_State*L,bool tracked=false){
    auto&s=self(L);const char*path=luaL_checkstring(L,1),*method=luaL_optstring(L,2,"read");
    if(s.isolated_)return luaL_error(L,"worker_system_calls_require_main_application");
    if(s.preparing_&&(tracked||!s.core_.readOnly(path,method)))return luaL_error(L,"Preparation requires a driver-declared read method");
    if(s.loading_)return luaL_error(L,"System calls belong in on_start/on_event, not module loading");
    if(!strcmp(path,"/sys/runtime")&&strcmp(method,"read"))return luaL_error(L,"Runtime mutation is not reentrant");
    if(++s.calls_>s.callLimit_)return luaL_error(L,"system_call_budget_exhausted");
    s.cleanup();s.input_=s.document();s.output_=s.document();
    if(!s.input_||!s.output_){s.cleanup();return luaL_error(L,"system_memory_low");}
    s.jsonBudget_=0;bool valid=lua_isnoneornil(L,3)||fromLua(L,3,s.input_->to<JsonVariant>());
    if(!valid||(!lua_isnoneornil(L,3)&&!s.input_->is<JsonObject>())||s.input_->overflowed()||measureJson(*s.input_)>4096){s.cleanup();return luaL_error(L,"Argument budget or type rejected");}
    auto result=(*s.output_)["result"].to<JsonObject>();const char*e=tracked?s.core_.request(path,method,s.input_->as<JsonObjectConst>(),result,s.owner_):s.core_.invoke(path,method,s.input_->as<JsonObjectConst>(),result,s.owner_);
    if(s.output_->overflowed()){s.cleanup();return luaL_error(L,"system_response_memory_low");}
    (*s.output_)["ok"]=!e;if(e)(*s.output_)["error"]=e;toLua(L,s.output_->as<JsonVariantConst>());s.cleanup();return 1;
  }
  static int invoke(lua_State*L){return invokeCommon(L);}
  static int trackedInvoke(lua_State*L){return invokeCommon(L,true);}
  static int completion(lua_State*L){
    auto&s=self(L);auto id=uint32_t(luaL_checkinteger(L,1));bool release=lua_toboolean(L,2);
    if(release){auto error=s.core_.completions().release(id,s.owner_);lua_pushboolean(L,!error);return 1;}
    s.cleanup();s.output_=s.document();if(!s.output_)return luaL_error(L,"system_memory_low");
    auto error=s.core_.completions().read(id,s.owner_,s.output_->to<JsonObject>());
    if(error){s.cleanup();return luaL_error(L,"%s",error);}toLua(L,s.output_->as<JsonVariantConst>());s.cleanup();return 1;
  }
  static void setSubscription(lua_State*L,const char*path,bool enabled,bool managed){
    auto&s=self(L);if(s.isolated_)luaL_error(L,"worker_subscriptions_unavailable");
    if(*path!='/'||strlen(path)>=48)luaL_error(L,"event_source_limit");
    unsigned i=0;for(;i<s.subscriptionCount_;++i)if(!strcmp(path,s.subscriptions_[i].path))break;
    if(i==s.subscriptionCount_){if(!enabled)return;if(i==16)luaL_error(L,"subscription_capacity");if(i==s.subscriptionCapacity_){auto next=static_cast<Subscription*>(s.core_.resources().reallocate(s.subscriptions_,(i+1)*sizeof(Subscription)));if(!next)luaL_error(L,"subscription_memory_low");s.subscriptions_=next;s.subscriptionCapacity_=i+1;}s.subscriptions_[i]={};copy(s.subscriptions_[i].path,48,path);++s.subscriptionCount_;}
    auto&entry=s.subscriptions_[i];if(!managed&&enabled&&!entry.manual)entry.manualAfter=s.core_.sequence();(managed?entry.callbacks:entry.manual)=enabled;
    if(!entry.manual&&!entry.callbacks){for(unsigned j=i+1;j<s.subscriptionCount_;++j)s.subscriptions_[j-1]=s.subscriptions_[j];s.subscriptions_[--s.subscriptionCount_]={};if(!s.subscriptionCount_){s.core_.resources().freeAllocation(s.subscriptions_);s.subscriptions_=nullptr;s.subscriptionCapacity_=0;}}
  }
  static int subscribe(lua_State*L){setSubscription(L,luaL_checkstring(L,1),true,false);return 0;}
  static int callbackListen(lua_State*L){auto&s=self(L);size_t size;const char*path=luaL_checklstring(L,1,&size);if(strlen(path)!=size)return luaL_error(L,"event_source_limit");setSubscription(L,path,lua_toboolean(L,2),true);s.callbackCount_=lua_tointeger(L,3);lua_pushinteger(L,s.core_.sequence());return 1;}
  static int callbackSchedule(lua_State*L){auto&s=self(L);s.callbacksPending_=true;lua_pushinteger(L,s.core_.now());return 1;}
  static int callbackResult(lua_State*L,int,lua_KContext){return lua_gettop(L);}
  static int callbackApi(lua_State*L){
    auto&s=self(L);if(s.isolated_)return luaL_error(L,"worker_subscriptions_unavailable");int args=lua_gettop(L);
    if(s.callbacksRef_==LUA_NOREF){
      if(luaL_loadbufferx(L,reinterpret_cast<const char*>(CALLBACK_BYTECODE),sizeof(CALLBACK_BYTECODE),"system callbacks","b")!=LUA_OK)return lua_error(L);
      lua_pushcfunction(L,callbackListen);lua_pushcfunction(L,invoke);lua_pushcfunction(L,callbackSchedule);lua_pushlightuserdata(L,&nullTag_);lua_pushcfunction(L,trackedInvoke);lua_pushcfunction(L,completion);
      lua_call(L,6,1);s.callbacksRef_=luaL_ref(L,LUA_REGISTRYINDEX);
    }
    lua_rawgeti(L,LUA_REGISTRYINDEX,s.callbacksRef_);lua_getfield(L,-1,lua_tostring(L,lua_upvalueindex(1)));lua_remove(L,-2);lua_insert(L,1);
    lua_callk(L,args,LUA_MULTRET,0,callbackResult);return lua_gettop(L);
  }
  void clearCallbacks(){
    if(state_&&callbacksRef_!=LUA_NOREF)luaL_unref(state_,LUA_REGISTRYINDEX,callbacksRef_);
    callbacksRef_=LUA_NOREF;callbacksPending_=false;callbackCount_=0;
    unsigned n=0;for(unsigned i=0;i<subscriptionCount_;++i)if(subscriptions_[i].manual){subscriptions_[n]=subscriptions_[i];subscriptions_[n++].callbacks=false;}subscriptionCount_=n;
  }
  static int log(lua_State*L){auto&s=self(L);if(s.isolated_||s.preparing_)return 0;const char*text=lua_tostring(L,1);s.cleanup();s.output_=s.document();if(s.output_){(*s.output_)["message"]=text&&strlen(text)<256?text:"log_value_limit";s.core_.emit("/sys/runtime","log",s.output_->as<JsonObjectConst>(),s.owner_);s.cleanup();}return 0;}
  static int decode(lua_State*L){auto&s=self(L);size_t n;const char*text=luaL_checklstring(L,1,&n);if(n>4096)return luaL_error(L,"json_limit");s.cleanup();s.input_=s.document();if(!s.input_)return luaL_error(L,"memory_low");if(deserializeJson(*s.input_,text,n)){s.cleanup();return luaL_error(L,"invalid_json");}toLua(L,s.input_->as<JsonVariantConst>());s.cleanup();return 1;}
  static int encode(lua_State*L){auto&s=self(L);s.cleanup();s.input_=s.document();if(!s.input_)return luaL_error(L,"memory_low");s.jsonBudget_=0;if(!fromLua(L,1,s.input_->to<JsonVariant>())||s.input_->overflowed()){s.cleanup();return luaL_error(L,"json_limit");}size_t n=measureJson(*s.input_);if(n>4096){s.cleanup();return luaL_error(L,"json_limit");}s.scratch_=(char*)malloc(n+1);if(!s.scratch_){s.cleanup();return luaL_error(L,"memory_low");}serializeJson(*s.input_,s.scratch_,n+1);lua_pushlstring(L,s.scratch_,n);s.cleanup();return 1;}
  static int init(lua_State*L){
    for(auto entry:{std::pair<const char*,lua_CFunction>{"_G",luaopen_base},{"table",luaopen_table},{"string",luaopen_string},{"math",luaopen_math},{"utf8",luaopen_utf8}}){luaL_requiref(L,entry.first,entry.second,1);lua_pop(L,1);}
    for(const char*name:{"dofile","loadfile","load","setmetatable"}){lua_pushnil(L);lua_setglobal(L,name);}
    // Lua pattern matching can run unbounded C backtracking without VM hooks.
    lua_getglobal(L,"string");for(const char*name:{"match","gmatch","gsub","find"}){lua_pushnil(L);lua_setfield(L,-2,name);}lua_pop(L,1);
    lua_getglobal(L,"table");lua_pushnil(L);lua_setfield(L,-2,"move");lua_pop(L,1);
    lua_pushcfunction(L,log);lua_setglobal(L,"print");lua_newtable(L);
    lua_pushcfunction(L,invoke);lua_setfield(L,-2,"call");lua_pushcfunction(L,subscribe);lua_setfield(L,-2,"subscribe");lua_pushcfunction(L,decode);lua_setfield(L,-2,"decode");lua_pushcfunction(L,encode);lua_setfield(L,-2,"encode");lua_pushlightuserdata(L,&nullTag_);lua_setfield(L,-2,"null");
    lua_newtable(L);lua_pushliteral(L,"notes.array");lua_setfield(L,-2,"__metatable");lua_rawsetp(L,LUA_REGISTRYINDEX,&arrayTag_);
    lua_pushcfunction(L,array);lua_setfield(L,-2,"array");lua_pushcfunction(L,unsubscribe);lua_setfield(L,-2,"unsubscribe");
    lua_pushcfunction(L,resolve);lua_setfield(L,-2,"resolve");lua_pushcfunction(L,findLiteral);lua_setfield(L,-2,"find");lua_pushcfunction(L,requireModule);lua_setfield(L,-2,"require");
    const char*names[]={"hex","unhex","base64","unbase64"};for(int i=0;i<4;++i){lua_pushinteger(L,i);lua_pushcclosure(L,bytes,1);lua_setfield(L,-2,names[i]);}
    for(auto name:{"on","off","request"}){lua_pushstring(L,name);lua_pushcclosure(L,callbackApi,1);lua_setfield(L,-2,name);}
    // Keep the upstream incremental collector defaults. The allocator enforces
    // the VM ceiling and Lua performs emergency collection on allocation failure.
    // A pause of100 kept rescanning the live heap during ordinary API calls.
    lua_setglobal(L,"notes");return 0;
  }
  static int run(lua_State*L){auto&s=self(L);
    if(s.operation_==0){if(s.compileChunk(L,s.source_,s.sourceSize_,s.chunk_)!=LUA_OK)return lua_error(L);lua_callk(L,0,0,0,continuation);return 0;}
    bool dispatch=s.operation_==2&&s.callbacksRef_!=LUA_NOREF;
    if(dispatch||s.operation_==6){lua_rawgeti(L,LUA_REGISTRYINDEX,s.callbacksRef_);lua_getfield(L,-1,dispatch?"dispatch":"flush");lua_remove(L,-2);}
    else lua_getglobal(L,s.operation_==1?"on_start":s.operation_==3?"on_stop":s.operation_==4?"compute":s.operation_==5?"on_prepare":"on_event");
    if(!lua_isfunction(L,-1)){lua_pop(L,1);if(s.operation_==4)return luaL_error(L,"compute_function_required");return 0;}
    int args=0;if(s.operation_==4){toLua(L,s.computeInput_);args=1;}
    if(s.operation_==2){s.cleanup();s.input_=s.document();if(!s.input_)return luaL_error(L,"memory_low");auto o=s.input_->to<JsonObject>();o["source"]=s.event_->path;o["event"]=s.event_->name;o["sequence"]=s.event_->sequence;o["at_ms"]=s.event_->time;s.output_=s.document();if(!s.output_)return luaL_error(L,"memory_low");if(deserializeJson(*s.output_,s.event_->data))return luaL_error(L,"event_decode_failed");o["data"]=s.output_->as<JsonVariantConst>();if(s.input_->overflowed())return luaL_error(L,"event_memory_low");toLua(L,s.input_->as<JsonVariantConst>());s.cleanup();args=1;
      if(dispatch){if(s.legacyEvent_)lua_getglobal(L,"on_event");else lua_pushnil(L);args=2;}
    }
    int results=(s.operation_==2||s.operation_==4)?1:0;lua_callk(L,args,results,results,continuation);return continuation(L,LUA_OK,results);
  }
  static int makeThread(lua_State*L){lua_newthread(L);return 1;}
  bool execute(int operation){
    consumed_=false;if(!state_||executing_||fault_||(stopped_&&operation!=3))return false;operation_=operation;steps_=0;calls_=0;executing_=true;
    lua_pushcfunction(state_,makeThread);if(lua_pcall(state_,0,1,0)!=LUA_OK){copy(error_,sizeof(error_),"coroutine_memory_low");lua_pop(state_,1);executing_=false;fault_=true;releaseOwned();return false;}
    lua_State*thread=lua_tothread(state_,-1);*(LuaRuntime**)lua_getextraspace(thread)=this;lua_sethook(thread,hook,LUA_MASKCOUNT,1000);lua_pushcfunction(thread,run);
    int results=0;int status=lua_resume(thread,nullptr,0,&results);consumed_=status==LUA_OK&&operation==2&&results&&lua_toboolean(thread,-1);
    cleanup();
    if(status!=LUA_OK){releaseOwned();if(!copy(error_,sizeof(error_),status==LUA_YIELD?"instruction_budget_exhausted":lua_tostring(thread,-1)))copy(error_,sizeof(error_),"lua_execution_error");fault_=true;}
    lua_sethook(thread,nullptr,0,0);lua_pop(state_,1);
    // Allocations drive Lua's incremental collector. Advance it at callback
    // boundaries rather than rescanning the entire live VM for every input
    // sample/timer; Lua also retries allocations through its emergency GC.
    lua_gc(state_,LUA_GCSTEP,0);executing_=false;return status==LUA_OK&&!fault_;
  }
public:
  LuaRuntime(Core&core,size_t budget=0,uint32_t steps=20000,uint32_t calls=32):core_(core),budget_(budget?budget:core.resources().defaultVmLimit()),stepLimit_(steps),callLimit_(calls),owner_(core.acquireOwner()){reservation_=core_.resources().acquireApplication(budget_,owner_);if(!reservation_)copy(error_,sizeof error_,"system_memory_budget");}
  ~LuaRuntime(){releaseOwned();loading_=true;cleanup();if(state_)lua_close(state_);core_.resources().freeAllocation(modules_);core_.resources().freeAllocation(subscriptions_);core_.resources().release(reservation_);}
  void fileContext(const char*file,Reader reader,void*context){reader_=reader;readerContext_=context;copy(chunk_,sizeof chunk_,*file?file:"application");copy(base_,sizeof base_,file);auto end=strrchr(base_,'/');if(end==base_)end[1]=0;else if(end)*end=0;else strcpy(base_,"/");}
  void compilationCache(RuntimeCompileCache*cache){if(!state_)compileCache_=cache;}
  bool stop(){if(stopped_)return !fault_;stopped_=true;bool ok=fault_?false:execute(3);releaseOwned();return ok;}
  void jsonAllocationBackend(ArduinoJson::Allocator*allocator){jsonAllocator_=allocator;}
  void allocationBackend(Resize resize,void*context){if(!state_){resize_=resize;allocatorContext_=context;}}
  bool load(const char*source,size_t n){
    if(state_||!owner_||!reservation_)return false;
    if(n>8192||budget_<16384)return false;state_=lua_newstate(allocate,this);if(!state_){copy(error_,sizeof(error_),"lua_memory_low");return false;}
    *(LuaRuntime**)lua_getextraspace(state_)=this;lua_pushcfunction(state_,init);if(lua_pcall(state_,0,0,0)!=LUA_OK){copy(error_,sizeof(error_),lua_tostring(state_,-1));return false;}
    source_=source;sourceSize_=n;loading_=true;bool ok=execute(0);loading_=false;source_=nullptr;sourceSize_=0;return ok;
  }
  void isolate(void(*yield)(void*),bool(*cancelled)(void*),void*context){isolated_=true;yield_=yield;cancelled_=cancelled;executionContext_=context;}
  bool compute(JsonVariantConst input,JsonVariant output){computeInput_=input;computeOutput_=output;bool ok=execute(4);computeInput_=JsonVariantConst();computeOutput_=JsonVariant();return ok;}
  bool prepare(){preparing_=true;bool ok=execute(5);preparing_=false;return ok;}
  bool start(){return execute(1);}
  bool flushCallbacks(){if(callbacksRef_==LUA_NOREF||(!callbacksPending_&&!core_.completions().ready(owner_))||fault_||stopped_)return false;callbacksPending_=false;return execute(6);}
  bool event(const Event&e){if(fault_||stopped_)return false;
    if(e.targetOwner&&e.targetOwner!=owner_)return false;
    bool match=false;legacyEvent_=false;for(unsigned i=0;i<subscriptionCount_;++i)if(!strcmp(e.path,subscriptions_[i].path)){legacyEvent_=subscriptions_[i].manual&&eventAfter(e.sequence,subscriptions_[i].manualAfter);match=legacyEvent_||subscriptions_[i].callbacks;}if(!match)return false;event_=&e;execute(2);event_=nullptr;return consumed_;}
  bool admitted()const{return reservation_!=0;}
  bool executing()const{return executing_;}const char*error()const{return error_;}
  void describe(JsonObject o)const{o["engine"]="lua-5.4.8";o["native_call_depth_limit"]=LUAI_MAXCCALLS;o["owner"]=owner_;o["modules_loaded"]=moduleCount_;o["subscription_limit"]=16;o["callback_allocation"]="on_demand_vm_budget";o["callbacks"]=callbackCount_;o["callback_flush_pending"]=callbacksPending_;o["base_path"]=JsonString(base_,strlen(base_));o["stopped"]=stopped_;o["memory_used"]=used_;o["memory_peak"]=peak_;o["memory_budget"]=budget_;o["memory_region"]=core_.resources().allocationRegion(reservation_);o["instruction_budget"]=stepLimit_;o["system_call_budget"]=callLimit_;o["last_system_calls"]=calls_;o["last_instructions"]=steps_;o["fault"]=fault_;o["error"]=JsonString(error_,strlen(error_));auto subscriptions=o["subscriptions"].to<JsonArray>();for(unsigned i=0;i<subscriptionCount_;++i)subscriptions.add(JsonString(subscriptions_[i].path,strlen(subscriptions_[i].path)));}
};
}
