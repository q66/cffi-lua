#ifndef LIB_HH
#define LIB_HH

#include <string>

#include "lua.hh"

namespace lib {

using handle = void *;
using func = void (*)();

handle load(char const *path, lua_State *L, bool global = false);
handle get_c();

void close(handle h);

func get_func(handle h, char const *name);
void *get_var(handle h, char const *name);

} /* namespace lib */

#endif /* LIB_HH */
