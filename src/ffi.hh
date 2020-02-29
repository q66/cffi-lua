#ifndef FFI_HH
#define FFI_HH

#include <cstddef>

#include "libffi.hh"

#include "lua.hh"
#include "lib.hh"
#include "ast.hh"

namespace ffi {

template<typename T>
struct cdata {
    ast::c_type decl;
    T val;

    void *get_addr() {
        if (decl.type() == ast::C_BUILTIN_PTR) {
            return *reinterpret_cast<void **>(&val);
        }
        return &val;
    }
};

/* data used for function types */
struct alignas(std::max_align_t) fdata {
    void (*sym)();
    ffi_cif cif;
    ast::c_value args[];
};

/* data used for large (generally struct) types */
template<typename T>
struct alignas(std::max_align_t) sdata {
    T val;
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
