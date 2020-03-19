#ifndef FFI_HH
#define FFI_HH

#include <cstddef>
#include <limits>
#include <type_traits>
#include <list>

#include "libffi.hh"

#include "lua.hh"
#include "lib.hh"
#include "ast.hh"

namespace ffi {

enum metatype_flag {
    /* all versions */
    METATYPE_FLAG_ADD      = 1 << 0,
    METATYPE_FLAG_SUB      = 1 << 1,
    METATYPE_FLAG_MUL      = 1 << 2,
    METATYPE_FLAG_DIV      = 1 << 3,
    METATYPE_FLAG_MOD      = 1 << 4,
    METATYPE_FLAG_POW      = 1 << 5,
    METATYPE_FLAG_UNM      = 1 << 6,
    METATYPE_FLAG_CONCAT   = 1 << 7,
    METATYPE_FLAG_LEN      = 1 << 8,
    METATYPE_FLAG_EQ       = 1 << 9,
    METATYPE_FLAG_LT       = 1 << 10,
    METATYPE_FLAG_LE       = 1 << 11,
    METATYPE_FLAG_INDEX    = 1 << 12,
    METATYPE_FLAG_NEWINDEX = 1 << 13,
    METATYPE_FLAG_CALL     = 1 << 14,
    METATYPE_FLAG_GC       = 1 << 15,
    METATYPE_FLAG_TOSTRING = 1 << 16,

#if LUA_VERSION_NUM > 501
    /* lua 5.2+ */
    METATYPE_FLAG_PAIRS    = 1 << 17,

#if LUA_VERSION_NUM == 502
    /* lua 5.2 only */
    METATYPE_FLAG_IPAIRS   = 1 << 18,
#endif

#if LUA_VERSION_NUM > 502
    /* lua 5.3+ */
    METATYPE_FLAG_IDIV     = 1 << 19,
    METATYPE_FLAG_BAND     = 1 << 20,
    METATYPE_FLAG_BOR      = 1 << 21,
    METATYPE_FLAG_BXOR     = 1 << 22,
    METATYPE_FLAG_BNOT     = 1 << 23,
    METATYPE_FLAG_SHL      = 1 << 24,
    METATYPE_FLAG_SHR      = 1 << 25,
#endif /* LUA_VERSION_NUM > 502 */
#endif /* LUA_VERSION_NUM > 501 */
};

static inline constexpr auto metafield_name(metatype_flag flag) {
    switch (flag) {
        case METATYPE_FLAG_ADD:      return "__add";
        case METATYPE_FLAG_SUB:      return "__sub";
        case METATYPE_FLAG_MUL:      return "__mul";
        case METATYPE_FLAG_DIV:      return "__div";
        case METATYPE_FLAG_MOD:      return "__mod";
        case METATYPE_FLAG_POW:      return "__pow";
        case METATYPE_FLAG_UNM:      return "__unm";
        case METATYPE_FLAG_CONCAT:   return "__concat";
        case METATYPE_FLAG_LEN:      return "__len";
        case METATYPE_FLAG_EQ:       return "__eq";
        case METATYPE_FLAG_LT:       return "__lt";
        case METATYPE_FLAG_LE:       return "__le";
        case METATYPE_FLAG_INDEX:    return "__index";
        case METATYPE_FLAG_NEWINDEX: return "__newindex";
        case METATYPE_FLAG_CALL:     return "__call";
        case METATYPE_FLAG_GC:       return "__gc";
        case METATYPE_FLAG_TOSTRING: return "__tostring";
#if LUA_VERSION_NUM > 501
        case METATYPE_FLAG_PAIRS:    return "__pairs";
#if LUA_VERSION_NUM == 502
        case METATYPE_FLAG_IPAIRS:   return "__ipairs";
#endif
#if LUA_VERSION_NUM > 502
        case METATYPE_FLAG_IDIV:     return "__idiv";
        case METATYPE_FLAG_BAND:     return "__band";
        case METATYPE_FLAG_BOR:      return "__bor";
        case METATYPE_FLAG_BXOR:     return "__bxor";
        case METATYPE_FLAG_BNOT:     return "__bnot";
        case METATYPE_FLAG_SHL:      return "__shl";
        case METATYPE_FLAG_SHR:      return "__shr";
#endif /* LUA_VERSION_NUM > 502 */
#endif /* LUA_VERSION_NUM > 501 */
        default: break;
    }
    return "";
}

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
    /* auxiliary data that can be used by different cdata
     *
     * vararg functions store the number of arguments they have storage
     * prepared for here to avoid reallocating every time; arrays store
     * the "weak flag" here which indicates they only point to others'
     * memory rather than having their own
     */
    int aux;
    alignas(arg_stor_t) T val;

    void *get_addr() {
        switch (decl.type()) {
            case ast::C_BUILTIN_PTR:
            case ast::C_BUILTIN_REF:
            case ast::C_BUILTIN_FUNC:
                return *reinterpret_cast<void **>(&val);
            default:
                break;
        }
        return &val;
    }

    void *get_deref_addr() {
        if (decl.type() == ast::C_BUILTIN_REF) {
            switch (decl.ptr_base().type()) {
                case ast::C_BUILTIN_PTR:
                case ast::C_BUILTIN_FUNC:
                    return **reinterpret_cast<void ***>(&val);
                default:
                    return *reinterpret_cast<void **>(&val);
            }
        }
        return get_addr();
    }
};

static constexpr size_t cdata_value_base() {
    /* can't use cdata directly for the offset, as it's not considered
     * a standard layout type because of ast::c_type, but we don't care
     * about that, we just want to know which offset val is at
     */
    using T = struct {
        alignas(ast::c_type) char tpad[sizeof(ast::c_type)];
        int pad1, pad2;
        arg_stor_t val;
    };
    return offsetof(T, val);
}

/* careful with this; use only if you're sure you have cdata at the index
 * as otherwise it will underflow size_t and get you a ridiculous value
 */
static inline size_t cdata_value_size(lua_State *L, int idx) {
    return lua_rawlen(L, idx) - cdata_value_base();
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
    auto *cd = static_cast<cdata<ffi::noval> *>(
        lua_newuserdata(L, vals + cdata_value_base())
    );
    new (&cd->decl) ast::c_type{std::move(tp)};
    cd->gc_ref = LUA_REFNIL;
    cd->aux = 0;
    lua::mark_cdata(L);
    return *cd;
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
void destroy_closure(closure_data *cd);

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

static inline bool metatype_getfield(lua_State *L, int mt, char const *fname) {
    luaL_getmetatable(L, lua::CFFI_CDATA_MT);
    lua_getfield(L, -1, "__ffi_metatypes");
    lua_rawgeti(L, -1, mt);
    if (lua_istable(L, -1)) {
        lua_getfield(L, -1, fname);
        if (!lua_isnil(L, -1)) {
            lua_insert(L, -4);
            lua_pop(L, 3);
            return true;
        } else {
            lua_pop(L, 1);
        }
    }
    lua_pop(L, 3);
    return false;
}

template<typename T>
static inline T check_arith(lua_State *L, int idx) {
    auto *cd = testcdata<arg_stor_t>(L, idx);
    if (!cd) {
        if (std::is_integral<T>::value) {
            return T(luaL_checkinteger(L, idx));
        }
        return T(luaL_checknumber(L, idx));
    }
    auto gf = [](int itp, arg_stor_t const &av, T &rv) {
        switch (itp) {
            case ast::C_BUILTIN_ENUM:
                /* TODO: large enums */
                rv = T(av.as<int>()); break;
            case ast::C_BUILTIN_BOOL:
                rv = T(av.as<bool>()); break;
            case ast::C_BUILTIN_CHAR:
                rv = T(av.as<char>()); break;
            case ast::C_BUILTIN_SCHAR:
                rv = T(av.as<signed char>()); break;
            case ast::C_BUILTIN_UCHAR:
                rv = T(av.as<unsigned char>()); break;
            case ast::C_BUILTIN_SHORT:
                rv = T(av.as<short>()); break;
            case ast::C_BUILTIN_USHORT:
                rv = T(av.as<unsigned short>()); break;
            case ast::C_BUILTIN_INT:
                rv = T(av.as<int>()); break;
            case ast::C_BUILTIN_UINT:
                rv = T(av.as<unsigned int>()); break;
            case ast::C_BUILTIN_LONG:
                rv = T(av.as<long>()); break;
            case ast::C_BUILTIN_ULONG:
                rv = T(av.as<unsigned long>()); break;
            case ast::C_BUILTIN_LLONG:
                rv = T(av.as<long long>()); break;
            case ast::C_BUILTIN_ULLONG:
                rv = T(av.as<unsigned long long>()); break;
            case ast::C_BUILTIN_FLOAT:
                rv = T(av.as<float>()); break;
            case ast::C_BUILTIN_DOUBLE:
                rv = T(av.as<double>()); break;
            case ast::C_BUILTIN_LDOUBLE:
                rv = T(av.as<long double>()); break;
            default:
                return false;
        }
        return true;
    };
    int tp = cd->decl.type();
    if (tp == ast::C_BUILTIN_REF) {
        T ret;
        if (gf(cd->decl.ptr_base().type(), *cd->val.as<arg_stor_t *>(), ret)) {
            return ret;
        }
    } else {
        T ret;
        if (gf(tp, cd->val, ret)) {
            return ret;
        }
    }
    if (std::is_integral<T>::value) {
        return T(luaL_checkinteger(L, idx));
    }
    return T(luaL_checknumber(L, idx));
}

static inline ast::c_expr_type check_arith_expr(
    lua_State *L, int idx, ast::c_value &iv
) {
    auto *cd = testcdata<arg_stor_t>(L, idx);
    if (!cd) {
        /* some logic for conversions of lua numbers into cexprs */
        static_assert(
            std::is_integral<lua_Number>::value
                ? (sizeof(lua_Number) <= sizeof(long long))
                : (sizeof(lua_Number) <= sizeof(long double)),
            "invalid lua_Number format"
        );
        auto n = luaL_checknumber(L, idx);
        if (std::is_integral<lua_Number>::value) {
            if (std::is_signed<lua_Number>::value) {
                if (sizeof(lua_Number) <= sizeof(int)) {
                    iv.i = n;
                    return ast::c_expr_type::INT;
                } else if (sizeof(lua_Number) <= sizeof(long)) {
                    iv.l = n;
                    return ast::c_expr_type::LONG;
                } else {
                    iv.ll = n;
                    return ast::c_expr_type::LLONG;
                }
            } else {
                if (sizeof(lua_Number) <= sizeof(unsigned int)) {
                    iv.u = n;
                    return ast::c_expr_type::UINT;
                } else if (sizeof(lua_Number) <= sizeof(unsigned long)) {
                    iv.ul = n;
                    return ast::c_expr_type::ULONG;
                } else {
                    iv.ull = n;
                    return ast::c_expr_type::ULLONG;
                }
            }
        } else if (sizeof(lua_Number) <= sizeof(float)) {
            iv.f = n;
            return ast::c_expr_type::FLOAT;
        } else if (sizeof(lua_Number) <= sizeof(double)) {
            iv.d = n;
            return ast::c_expr_type::DOUBLE;
        }
        iv.ld = n;
        return ast::c_expr_type::LDOUBLE;
    }
    auto gf = [](int itp, arg_stor_t const &av, ast::c_value &v) {
        switch (itp) {
            case ast::C_BUILTIN_ENUM:
                /* TODO: large enums */
                v.i = av.as<int>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_BOOL:
                v.i = av.as<bool>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_CHAR:
                v.i = av.as<char>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_SCHAR:
                v.i = av.as<signed char>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_UCHAR:
                v.i = av.as<unsigned char>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_SHORT:
                v.i = av.as<short>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_USHORT:
                v.i = av.as<unsigned short>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_INT:
                v.i = av.as<int>();
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_UINT:
                v.u = av.as<unsigned int>();
                return ast::c_expr_type::UINT;
            case ast::C_BUILTIN_LONG:
                v.l = av.as<long>();
                return ast::c_expr_type::LONG;
            case ast::C_BUILTIN_ULONG:
                v.ul = av.as<unsigned long>();
                return ast::c_expr_type::ULONG;
            case ast::C_BUILTIN_LLONG:
                v.ll = av.as<long long>();
                return ast::c_expr_type::LLONG;
            case ast::C_BUILTIN_ULLONG:
                v.ull = av.as<unsigned long long>();
                return ast::c_expr_type::ULLONG;
            case ast::C_BUILTIN_FLOAT:
                v.f = av.as<float>();
                return ast::c_expr_type::FLOAT;
            case ast::C_BUILTIN_DOUBLE:
                v.d = av.as<double>();
                return ast::c_expr_type::DOUBLE;
            case ast::C_BUILTIN_LDOUBLE:
                v.ld = av.as<long double>();
                return ast::c_expr_type::LDOUBLE;
            default:
                return ast::c_expr_type::INVALID;
        }
    };
    ast::c_expr_type ret;
    int tp = cd->decl.type();
    if (tp == ast::C_BUILTIN_REF) {
        ret = gf(cd->decl.ptr_base().type(), *cd->val.as<arg_stor_t *>(), iv);
    } else {
        ret = gf(tp, cd->val, iv);
    }
    if (ret == ast::c_expr_type::INVALID) {
        luaL_checknumber(L, idx);
    }
    /* unreachable */
    return ret;
}

static inline cdata<arg_stor_t> &make_cdata_arith(
    lua_State *L, ast::c_expr_type et, ast::c_value const &cv
) {
    auto bt = ast::to_builtin_type(et);
    if (bt == ast::C_BUILTIN_INVALID) {
        luaL_error(L, "invalid value type");
    }
    auto tp = ast::c_type{bt, 0};
    auto as = tp.alloc_size();
    auto &cd = newcdata(L, std::move(tp), as);
    memcpy(&cd.val, &cv, as);
    return *reinterpret_cast<cdata<arg_stor_t> *>(&cd);
}

static inline std::string lua_serialize(lua_State *L, int idx) {
    auto *cd = testcdata<noval>(L, idx);
    if (cd) {
        return cd->decl.serialize();
    }
    return lua_typename(L, lua_type(L, idx));
}

} /* namespace ffi */

#endif /* FFI_HH */
