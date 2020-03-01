#ifndef FFI_HH
#define FFI_HH

#include <cstddef>
#include <list>

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
        switch (decl.type()) {
            case ast::C_BUILTIN_PTR:
            case ast::C_BUILTIN_FUNC:
            case ast::C_BUILTIN_FPTR:
                return *reinterpret_cast<void **>(&val);
            default:
                break;
        }
        return &val;
    }
};

struct closure_data {
    ffi_closure *closure = nullptr;
    lua_State *L = nullptr;
    int fref = LUA_REFNIL;
    std::list<closure_data **> refs{};
    ~closure_data() {
        if (!closure) {
            return;
        }
        /* invalidate any registered references to the closure data */
        while (!refs.empty()) {
            *refs.front() = nullptr;
            refs.pop_front();
        }
        luaL_unref(L, LUA_REGISTRYINDEX, fref);
        ffi_closure_free(closure);
    }
};

/* data used for function types */
struct alignas(std::max_align_t) fdata {
    void (*sym)();
    closure_data *cd; /* only for callbacks, otherwise nullptr */
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

void make_cdata_func(
    lua_State *L, void (*funp)(), ast::c_function const &func,
    int cbt = ast::C_BUILTIN_FUNC, closure_data *cd = nullptr
);

bool prepare_cif(cdata<fdata> &fud);
int call_cif(cdata<fdata> &fud, lua_State *L);

int lua_push_cdata(lua_State *L, ast::c_type const &tp, void *value);
void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, ast::c_value *stor, int index
);

} /* namespace ffi */

#endif /* FFI_HH */
