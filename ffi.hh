#ifndef FFI_HH
#define FFI_HH

#include <string>

#include <lua.hpp>

#include <ffi.h>

#include "parser.hh"

namespace ffi {

ffi_type *get_ffi_type(parser::c_type const &tp);

void lua_push_cdata(lua_State *L, parser::c_type const &tp, void *value);
void *lua_check_cdata(
    lua_State *L, parser::c_type const &tp, parser::c_value *stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
