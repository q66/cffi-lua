#ifndef LIB_HH
#define LIB_HH

#include <string>

#include "lua.hh"

namespace lib {

using handle = void *;
using func = void (*)();

handle load(char const *path, lua_State *L, bool global = false);

void close(handle h);

void *get_sym(handle h, char const *name);

} /* namespace lib */

#endif /* LIB_HH */
