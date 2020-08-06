#ifndef LIB_HH
#define LIB_HH

#include "lua.hh"

namespace lib {

using handle = void *;
using func = void (*)();

struct c_lib {
    handle h;
    int cache;
};

void load(c_lib *cl, char const *path, lua_State *L, bool global = false);

void close(c_lib *cl, lua_State *L);

void *get_sym(c_lib const *cl, lua_State *L, char const *name);

bool is_c(c_lib const *cl);

} /* namespace lib */

#endif /* LIB_HH */
