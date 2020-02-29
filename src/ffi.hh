#ifndef FFI_HH
#define FFI_HH

#include "libffi.hh"

#include "lua.hh"
#include "lib.hh"
#include "ast.hh"

namespace ffi {

template<typename T>
struct cdata {
    ast::c_type decl;
    T val;
};

struct alignas(alignof(cdata<ast::c_value>)) fdata {
    void (*sym)();
    ffi_cif cif;
    ast::c_value args[];
};

void make_cdata(
    lua_State *L, lib::handle dl, ast::c_object const *obj, char const *name
);

bool prepare_cif(cdata<fdata> &fud);
int call_cif(cdata<fdata> &fud, lua_State *L);

int lua_push_cdata(lua_State *L, ast::c_type const &tp, void *value);
void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, ast::c_value *stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
