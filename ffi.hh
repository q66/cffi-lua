#ifndef FFI_HH
#define FFI_HH

#include <string>

#include <lua.hpp>

#include <ffi.h>

#include "parser.hh"

namespace ffi {

template<typename T>
struct cdata {
    parser::c_object *decl;
    T val;
};

struct fdata {
    void (*sym)();
    ffi_cif cif;
};

ffi_type *get_ffi_type(parser::c_type const &tp);

void lua_push_cdata(lua_State *L, parser::c_type const &tp, void *value);
void *lua_check_cdata(
    lua_State *L, parser::c_type const &tp, parser::c_value *stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
