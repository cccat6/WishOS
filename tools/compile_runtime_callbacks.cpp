// Build against the repository's Lua 5.4.8 library. Only the trusted runtime
// library is bytecode; application/module loaders still accept text exclusively.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "RuntimeCallbacks.h"
extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
}
static int collect(lua_State*,const void*p,size_t n,void*context){auto&out=*static_cast<std::vector<unsigned char>*>(context);auto bytes=static_cast<const unsigned char*>(p);out.insert(out.end(),bytes,bytes+n);return 0;}
int main(int argc,char**argv){
 if(argc!=2)return 2;auto state=luaL_newstate();if(!state)return 1;
 if(luaL_loadbufferx(state,notesos::CALLBACK_LIBRARY,sizeof(notesos::CALLBACK_LIBRARY)-1,"system callbacks","t")){std::fprintf(stderr,"%s\n",lua_tostring(state,-1));lua_close(state);return 1;}
 std::vector<unsigned char>bytes;if(lua_dump(state,collect,&bytes,1)){lua_close(state);return 1;}lua_close(state);
 auto file=std::fopen(argv[1],"w");if(!file)return 1;
 std::fprintf(file,"#pragma once\n#include <stdint.h>\n// Generated with vendored Lua 5.4.8; stripped debug info, int64/double ABI.\nnamespace notesos {\ninline constexpr uint64_t CALLBACK_BYTECODE_SOURCE_HASH=0x%llxULL;\ninline constexpr unsigned char CALLBACK_BYTECODE[]={\n",(unsigned long long)notesos::callbackSourceHash());
 for(size_t i=0;i<bytes.size();++i)std::fprintf(file,"0x%02x,%s",bytes[i],i%16==15?"\n":"");
 std::fprintf(file,"\n};\n}\n");if(std::fclose(file))return 1;std::printf("%zu bytes\n",bytes.size());
}
