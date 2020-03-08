#ifndef FFI_HH
#define FFI_HH

#include <cstddef>
#include <type_traits>
#include <list>

#include "libffi.hh"

#include "lua.hh"
#include "lib.hh"
#include "ast.hh"

namespace ffi {

struct arg_stor_t {
    std::max_align_t pad;

    /* only use with types that will fit */
    template<typename T>
    T as() const {
        return *reinterpret_cast<T const *>(this);
    }

    template<typename T>
    T &as() {
        return *reinterpret_cast<T *>(this);
    }
};

struct noval {};

template<typename T>
struct cdata {
    ast::c_type decl;
    int gc_ref;
    int aux; /* auxiliary data that can be used by different cdata */
    alignas(arg_stor_t) T val;

    void *get_addr() {
        switch (decl.type()) {
            case ast::C_BUILTIN_PTR:
            case ast::C_BUILTIN_REF:
            case ast::C_BUILTIN_FUNC:
            case ast::C_BUILTIN_FPTR:
                return *reinterpret_cast<void **>(&val);
            default:
                break;
        }
        return &val;
    }
};

/* careful with this; use only if you're sure you have cdata at the index
 * as otherwise it will underflow size_t and get you a ridiculous value
 */
static inline size_t cdata_value_size(lua_State *L, int idx) {
    /* can't use cdata directly for the offset, as it's not considered
     * a standard layout type because of ast::c_type, but we don't care
     * about that, we just want to know which offset val is at
     */
    using T = struct {
        alignas(ast::c_type) char tpad[sizeof(ast::c_type)];
        int pad1, pad2;
        arg_stor_t val;
    };
    return lua_rawlen(L, idx) - offsetof(T, val);
}

struct ctype {
    ast::c_type decl;
    int ct_tag;
};

struct closure_data {
    ffi_cif cif; /* closure data needs its own cif */
    ffi_closure *closure = nullptr;
    lua_State *L = nullptr;
    int fref = LUA_REFNIL;
    std::list<closure_data **> refs{};
    ffi_type *targs[];

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
struct fdata {
    void (*sym)();
    closure_data *cd; /* only for callbacks, otherwise nullptr */
    ffi_cif cif;
    arg_stor_t rarg;
    arg_stor_t args[];
};

template<typename T>
static inline cdata<T> &newcdata(
    lua_State *L, ast::c_type &&tp, size_t extra = 0
) {
    auto *cd = lua::newuserdata<cdata<T>>(L, extra);
    new (&cd->decl) ast::c_type{std::move(tp)};
    cd->gc_ref = LUA_REFNIL;
    cd->aux = 0;
    lua::mark_cdata(L);
    return *cd;
}

template<typename T>
static inline cdata<T> &newcdata(
    lua_State *L, ast::c_type const &tp, size_t extra = 0
) {
    return newcdata<T>(L, ast::c_type{tp}, extra);
}

static inline cdata<ffi::noval> &newcdata(
    lua_State *L, ast::c_type const &tp, size_t vals
) {
    return newcdata<ffi::noval>(L, tp, vals - sizeof(ffi::noval));
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

void destroy_cdata(lua_State *L, cdata<ffi::noval> &cd);

int call_cif(cdata<fdata> &fud, lua_State *L, size_t largs);

enum conv_rule {
    RULE_CONV = 0,
    RULE_PASS,
    RULE_CAST,
    RULE_RET
};

/* this pushes a value from `value` on the Lua stack; its type
 * and necessary conversions are done based on the info in `tp` and `rule`
 *
 * `lossy` implies that numbers will always be converted to a lua number
 */
int to_lua(
    lua_State *L, ast::c_type const &tp, void const *value, int rule,
    bool lossy = false
);

/* this returns a pointer to a C value counterpart of the Lua value
 * on the stack (as given by `index`) while checking types (`rule`)
 *
 * necessary conversions are done according to `tp`; `stor` is used to
 * write scalar values (therefore its alignment and size must be enough
 * to fit the converted value - the arg_stor_t type can store any scalar
 * so you can use that) while non-scalar values may have their address
 * returned directly
 */
void *from_lua(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    size_t &dsz, int rule
);

void get_global(lua_State *L, lib::handle dl, const char *sname);
void set_global(lua_State *L, lib::handle dl, char const *sname, int idx);

void make_cdata(lua_State *L, ast::c_type const &decl, int rule, int idx);

} /* namespace ffi */

#endif /* FFI_HH */
