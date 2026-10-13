  inline static char nullTag_,arrayTag_;
  static int array(lua_State*L){luaL_checktype(L,1,LUA_TTABLE);lua_pushvalue(L,1);lua_rawgetp(L,LUA_REGISTRYINDEX,&arrayTag_);lua_setmetatable(L,-2);return 1;}
  static bool isArray(lua_State*L,int index){if(!lua_getmetatable(L,index))return false;lua_rawgetp(L,LUA_REGISTRYINDEX,&arrayTag_);bool same=lua_rawequal(L,-1,-2);lua_pop(L,2);return same;}
  static int unsubscribe(lua_State*L){auto&s=self(L);const char*p=luaL_checkstring(L,1);for(unsigned i=0;i<s.subscriptionCount_;++i)if(!strcmp(p,s.subscriptions_[i])){for(unsigned j=i+1;j<s.subscriptionCount_;++j)strcpy(s.subscriptions_[j-1],s.subscriptions_[j]);--s.subscriptionCount_;break;}return 0;}
  static int resolve(lua_State*L){auto&s=self(L);const char*p=luaL_checkstring(L,1);char full[256];if(!normalizePath(s.base_,p,full,sizeof full))return luaL_error(L,"invalid_path");lua_pushstring(L,full);return 1;}
  static int findLiteral(lua_State*L){size_t n,z;const char*hay=luaL_checklstring(L,1,&n),*needle=luaL_checklstring(L,2,&z);lua_Integer start=luaL_optinteger(L,3,1);if(n>4096||z>256)return luaL_error(L,"literal_search_limit");if(start<1||start>lua_Integer(n+1)){lua_pushnil(L);return 1;}for(size_t at=start-1;at<=n&&z<=n-at;++at)if(!memcmp(hay+at,needle,z)){lua_pushinteger(L,at+1);lua_pushinteger(L,at+z);return 2;}lua_pushnil(L);return 1;}
  static int bytes(lua_State*L){
    auto&s=self(L);size_t n;const char*input=luaL_checklstring(L,1,&n);int kind=int(lua_tointeger(L,lua_upvalueindex(1)));
    size_t capacity=kind==0?n*2:kind==1?n/2:kind==2?((n+2)/3)*4:(n/4)*3;
    if(n>4096||capacity>4096)return luaL_error(L,"binary_chunk_limit");
    s.cleanup();s.scratch_=(char*)malloc(capacity+1);if(!s.scratch_)return luaL_error(L,"memory_low");size_t out=0;bool ok=true;
    const char*alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto nib=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    auto digit=[&](char c)->int{auto p=c?strchr(alphabet,c):nullptr;return p?int(p-alphabet):-1;};
    if(kind==0){for(size_t i=0;i<n;++i){uint8_t b=input[i];s.scratch_[out++]="0123456789abcdef"[b>>4];s.scratch_[out++]="0123456789abcdef"[b&15];}}
    if(kind==1){ok=n%2==0;for(size_t i=0;ok&&i<n;i+=2){int a=nib(input[i]),b=nib(input[i+1]);ok=a>=0&&b>=0;if(ok)s.scratch_[out++]=char((a<<4)|b);}}
    if(kind==2){for(size_t i=0;i<n;i+=3){uint32_t v=uint8_t(input[i])<<16;if(i+1<n)v|=uint8_t(input[i+1])<<8;if(i+2<n)v|=uint8_t(input[i+2]);s.scratch_[out++]=alphabet[v>>18];s.scratch_[out++]=alphabet[(v>>12)&63];s.scratch_[out++]=i+1<n?alphabet[(v>>6)&63]:'=';s.scratch_[out++]=i+2<n?alphabet[v&63]:'=';}}
    if(kind==3){ok=n%4==0;for(size_t i=0;ok&&i<n;i+=4){int a=digit(input[i]),b=digit(input[i+1]),c=digit(input[i+2]),d=digit(input[i+3]);bool pc=input[i+2]=='=',pd=input[i+3]=='=';ok=a>=0&&b>=0&&(c>=0||pc)&&(d>=0||pd)&&(!pc||pd)&&(!(pc||pd)||i+4==n)&&(!pc||(b&15)==0)&&(!pd||pc||(c&3)==0);if(!ok)break;uint32_t v=(a<<18)|(b<<12)|((pc?0:c)<<6)|(pd?0:d);s.scratch_[out++]=char(v>>16);if(!pc)s.scratch_[out++]=char(v>>8);if(!pd)s.scratch_[out++]=char(v);}}
    if(!ok){s.cleanup();return luaL_error(L,"invalid_binary_encoding");}lua_pushlstring(L,s.scratch_,out);s.cleanup();return 1;
  }
  static int requireModule(lua_State*L){
    auto&s=self(L);if(!s.loading_)return luaL_error(L,"Import modules during loading; use their functions in callbacks");
    if(!s.reader_)return luaL_error(L,"module_reader_unavailable");const char*name=luaL_checkstring(L,1);char full[256];if(!normalizePath(s.base_,name,full,sizeof full)||strlen(full)<4||strcmp(full+strlen(full)-4,".lua"))return luaL_error(L,"module_path");
    unsigned slot=0;for(;slot<s.moduleCount_;++slot)if(!strcmp(full,s.modules_[slot]))break;
    if(slot<s.moduleCount_){if(!s.moduleReady_[slot])return luaL_error(L,"cyclic_module");lua_rawgeti(L,LUA_REGISTRYINDEX,s.moduleRefs_[slot]);return 1;}
    if(slot==8)return luaL_error(L,"module_capacity");strcpy(s.modules_[s.moduleCount_++],full);
    char*source=nullptr;size_t size=0;const char*error=s.reader_(s.readerContext_,full,source,size);
    if(error){free(source);--s.moduleCount_;return luaL_error(L,"%s",error);}s.scratch_=source;
    if(size>8192){s.cleanup();--s.moduleCount_;return luaL_error(L,"module_size");}
    int status=luaL_loadbufferx(L,source,size,full,"t");s.cleanup();if(status!=LUA_OK){--s.moduleCount_;return lua_error(L);}
    char previous[256];strcpy(previous,s.base_);strcpy(s.base_,full);auto slash=strrchr(s.base_,'/');if(slash==s.base_)slash[1]=0;else *slash=0;
    status=lua_pcall(L,0,1,0);strcpy(s.base_,previous);if(status!=LUA_OK){return lua_error(L);}
    if(lua_isnil(L,-1)){lua_pop(L,1);lua_pushboolean(L,true);}lua_pushvalue(L,-1);s.moduleRefs_[slot]=luaL_ref(L,LUA_REGISTRYINDEX);s.moduleReady_[slot]=true;return 1;
  }
