#ifndef FFI_HH
#define FFI_HH

#include <string>

#include <lua.hpp>

#include <ffi.h>

#include "ast.hh"

namespace ffi {

template<typename T>
struct cdata {
    ast::c_object *decl;
    T val;
};

struct fdata {
    void (*sym)();
    ffi_cif cif;
    void *args[];
};

ffi_type *get_ffi_type(ast::c_type const &tp);

void lua_push_cdata(lua_State *L, ast::c_type const &tp, void *value);
void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, ast::c_value *stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
