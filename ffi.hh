#ifndef FFI_HH
#define FFI_HH

#include <string>

#include <lua.hpp>

#include <ffi.h>

#include "parser.hh"

namespace ffi {

ffi_type *get_ffi_type(parser::c_type const &tp);

void lua_push_cdata(lua_State *L, parser::c_type const &tp, ffi_arg value);
void lua_check_cdata(
    lua_State *L, parser::c_type const &tp, void **stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
