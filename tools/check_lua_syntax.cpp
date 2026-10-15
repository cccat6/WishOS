// Host-side text parser for the direct publishing client. Links exactly the
// vendored runtime Lua sources; no standard libraries or app code are executed.
extern "C" {
#include "lua/lua.h"
#include "lua/lauxlib.h"
}
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
int main(int argc,char**argv){if(argc==2&&std::string(argv[1])=="--version"){std::cout<<LUA_RELEASE<<"\n";return 0;}if(argc!=2)return 2;std::ifstream file(argv[1],std::ios::binary);if(!file){std::cerr<<"cannot read source\n";return 2;}std::string source((std::istreambuf_iterator<char>(file)),{});auto state=luaL_newstate();if(!state)return 2;int status=luaL_loadbufferx(state,source.data(),source.size(),argv[1],"t");if(status)std::cerr<<lua_tostring(state,-1)<<"\n";lua_close(state);return status?1:0;}
