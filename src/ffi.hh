#ifndef FFI_HH
#define FFI_HH

#include <string>

#include <ffi.h>

#include "lua.hh"
#include "ast.hh"

namespace ffi {

template<typename T>
struct cdata {
    ast::c_object const *decl;
    T val;
};

struct fdata {
    void (*sym)();
    ffi_cif cif;
    ast::c_value args[];
};

bool prepare_cif(cdata<fdata> &fud);
void call_cif(cdata<fdata> &fud, lua_State *L);

ffi_type *get_ffi_type(ast::c_type const &tp);

void lua_push_cdata(lua_State *L, ast::c_type const &tp, void *value);
void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, ast::c_value *stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
