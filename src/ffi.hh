#ifndef FFI_HH
#define FFI_HH

#include <cstddef>
#include <list>

#include "libffi.hh"

#include "lua.hh"
#include "lib.hh"
#include "ast.hh"

namespace ffi {

/* placeholder for no-value cdata */
struct noval {};

template<typename T>
struct cdata {
    ast::c_type decl;
    int gc_ref;
    size_t val_sz;
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

struct ctype {
    ast::c_type decl;
    int ct_tag;
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

template<typename T>
static inline cdata<T> &newcdata(
    lua_State *L, ast::c_type &&tp, size_t extra = 0
) {
    auto *cd = lua::newuserdata<cdata<T>>(L, extra);
    cd->val_sz = sizeof(T) + extra;
    new (&cd->decl) ast::c_type{std::move(tp)};
    cd->gc_ref = LUA_REFNIL;
    lua::mark_cdata(L);
    return *cd;
}

template<typename T>
static inline cdata<T> &newcdata(
    lua_State *L, ast::c_type const &tp, size_t extra = 0
) {
    return newcdata<T>(L, ast::c_type{tp}, extra);
}

template<typename ...A>
static inline ctype &newctype(lua_State *L, A &&...args) {
    auto *cd = lua::newuserdata<ctype>(L);
    cd->ct_tag = lua::CFFI_CTYPE_TAG;
    new (&cd->decl) ast::c_type{std::forward<A>(args)...};
    lua::mark_cdata(L);
    return *cd;
}

static inline bool iscdata(lua_State *L, int idx) {
    auto *p = static_cast<ctype *>(luaL_testudata(L, idx, lua::CFFI_CDATA_MT));
    return p && (p->ct_tag != lua::CFFI_CTYPE_TAG);
}

static inline bool isctype(lua_State *L, int idx) {
    auto *p = static_cast<ctype *>(luaL_testudata(L, idx, lua::CFFI_CDATA_MT));
    return p && (p->ct_tag == lua::CFFI_CTYPE_TAG);
}

static inline bool iscval(lua_State *L, int idx) {
    return luaL_testudata(L, idx, lua::CFFI_CDATA_MT);
}

template<typename T>
static inline bool isctype(cdata<T> const &cd) {
    return cd.gc_ref == lua::CFFI_CTYPE_TAG;
}

template<typename T>
static inline cdata<T> &checkcdata(lua_State *L, int idx) {
    auto ret = static_cast<cdata<T> *>(
        luaL_checkudata(L, idx, lua::CFFI_CDATA_MT)
    );
    if (isctype(*ret)) {
        lua::type_error(L, idx, "cdata");
    }
    return *ret;
}

template<typename T>
static inline cdata<T> *testcdata(lua_State *L, int idx) {
    auto ret = static_cast<cdata<T> *>(
        luaL_testudata(L, idx, lua::CFFI_CDATA_MT)
    );
    if (!ret || isctype(*ret)) {
        return nullptr;
    }
    return ret;
}

template<typename T>
static inline cdata<T> &tocdata(lua_State *L, int idx) {
    return *lua::touserdata<ffi::cdata<T>>(L, idx);
}

void make_cdata(
    lua_State *L, lib::handle dl, ast::c_object const *obj, char const *name
);

void make_cdata_func(
    lua_State *L, void (*funp)(), ast::c_function const &func,
    int cbt = ast::C_BUILTIN_FUNC, closure_data *cd = nullptr
);

bool prepare_cif(cdata<fdata> &fud);
int call_cif(cdata<fdata> &fud, lua_State *L);

/* this pushes a value from `value` on the Lua stack; its type
 * and necessary conversions are done based on the info in `tp`
 *
 * `lossy` implies that numbers will always be converted to a lua number
 */
int lua_push_cdata(
    lua_State *L, ast::c_type const &tp, void *value, bool lossy = false
);

/* this returns a pointer to a C value counterpart of the Lua value
 * on the stack (as given by `index`)
 *
 * necessary conversions are done according to `tp`; `stor` is used to
 * write scalar values (therefore its alignment and size must be enough
 * to fit the converted value - the ast::c_value type can store any scalar
 * so you can use that) while non-scalar values may have their address
 * returned directly
 */
void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    size_t &dsz
);

} /* namespace ffi */

#endif /* FFI_HH */
