#ifndef FFI_HH
#define FFI_HH

#include <cstring>
#include <cstddef>

#include "libffi.hh"

#include "lua.hh"
#include "lib.hh"
#include "ast.hh"
#include "util.hh"

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

    /* only for ctypes */
    METATYPE_FLAG_NEW      = 1 << 17,

#if LUA_VERSION_NUM > 501
    /* lua 5.2+ */
    METATYPE_FLAG_PAIRS    = 1 << 18,

#if LUA_VERSION_NUM == 502
    /* lua 5.2 only */
    METATYPE_FLAG_IPAIRS   = 1 << 19,
#endif

#if LUA_VERSION_NUM > 502
    /* lua 5.3+ */
    METATYPE_FLAG_IDIV     = 1 << 20,
    METATYPE_FLAG_BAND     = 1 << 21,
    METATYPE_FLAG_BOR      = 1 << 22,
    METATYPE_FLAG_BXOR     = 1 << 23,
    METATYPE_FLAG_BNOT     = 1 << 24,
    METATYPE_FLAG_SHL      = 1 << 25,
    METATYPE_FLAG_SHR      = 1 << 26,

    METATYPE_FLAG_NAME     = 1 << 27,
#if LUA_VERSION_NUM > 503
    METATYPE_FLAG_CLOSE    = 1 << 28,
#endif /* LUA_VERSION_NUM > 503 */
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
        case METATYPE_FLAG_NEW:      return "__new";
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
        case METATYPE_FLAG_NAME:     return "__name";
#if LUA_VERSION_NUM > 503
        case METATYPE_FLAG_CLOSE:    return "__close";
#endif /* LUA_VERSION_NUM > 503 */
#endif /* LUA_VERSION_NUM > 502 */
#endif /* LUA_VERSION_NUM > 501 */
        default: break;
    }
    return "";
}

static_assert((
    (sizeof(lua_Integer) <= sizeof(ffi::scalar_stor_t)) &&
    (alignof(lua_Integer) <= alignof(ffi::scalar_stor_t)) &&
    util::is_int<lua_Integer>::value
), "unsupported lua_Integer type");

/* lua_Number is supported either as a float or as an integer */
static_assert((
    (sizeof(lua_Number) <= sizeof(ffi::scalar_stor_t)) &&
    (alignof(lua_Number) <= alignof(ffi::scalar_stor_t)) &&
    (util::is_float<lua_Number>::value || util::is_int<lua_Number>::value)
), "unsupported lua_Number type");

struct cdata {
    ast::c_type decl;
    int gc_ref;
    /* auxiliary data that can be used by different cdata
     *
     * vararg functions store the number of arguments they have storage
     * prepared for here to avoid reallocating every time
     */
    int aux;

    template<typename D>
    cdata(D &&tp): decl{util::forward<D>(tp)} {}

    /* we always allocate enough userdata so that after the regular fields
     * of struct cdata, there is a data section, with enough space so that
     * the requested type can fit aligned to maximum scalar alignment we are
     * storing
     *
     * this is important, because lua_newuserdata may return misaligned pointers
     * (it only guarantees alignment of typically 8, while we typically need 16)
     * so we have to overallocate by a bit, then manually align the data
     */
    void *as_ptr() {
        return util::ptr_align(this + 1);
    }

    template<typename T>
    T &as() {
        return *static_cast<T *>(as_ptr());
    }

    void *as_deref_ptr() {
        if (decl.is_ref()) {
            return as<void *>();
        }
        return as_ptr();
    }

    template<typename T>
    T &as_deref() {
        return *static_cast<T *>(as_deref_ptr());
    }

    void *address_of() {
        if (decl.ptr_like()) {
            return as<void *>();
        }
        return as_deref_ptr();
    }
};

struct ctype {
    ast::c_type decl;
    int ct_tag;

    template<typename D>
    ctype(D &&tp): decl{util::forward<D>(tp)} {}
};

inline constexpr std::size_t cdata_pad_size() {
    auto csz = sizeof(cdata);
    auto mod = (csz % alignof(lua::user_align_t));
    /* size in multiples of lua userdata alignment */
    if (mod) {
        csz += alignof(lua::user_align_t) - mod;
    }
    /* this should typically not happen, unless configured that way */
    if (alignof(lua::user_align_t) >= alignof(util::max_aligned_t)) {
        return csz;
    }
    /* add the difference for internal alignment */
    csz += (alignof(util::max_aligned_t) - alignof(lua::user_align_t));
    /* this is what we will allocate, plus the data size */
    return csz;
}

struct closure_data {
    ffi_cif cif; /* closure data needs its own cif */
    int fref = LUA_REFNIL;
    lua_State *L = nullptr;
    ffi_closure *closure = nullptr;

    ~closure_data() {
        if (!closure) {
            return;
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
    ffi::scalar_stor_t rarg;

    ffi::scalar_stor_t *args() {
        return util::pun<ffi::scalar_stor_t *>(this + 1);
    }
};

static inline cdata &newcdata(
    lua_State *L, ast::c_type const &tp, std::size_t vals
) {
    auto ssz = cdata_pad_size() + vals;
    auto *cd = static_cast<cdata *>(lua_newuserdata(L, ssz));
    new (cd) cdata{tp.copy()};
    cd->gc_ref = LUA_REFNIL;
    cd->aux = 0;
    lua::mark_cdata(L);
    return *cd;
}

template<typename ...A>
static inline ctype &newctype(lua_State *L, A &&...args) {
    auto *cd = static_cast<ctype *>(lua_newuserdata(L, sizeof(ctype)));
    new (cd) ctype{ast::c_type{util::forward<A>(args)...}};
    cd->ct_tag = lua::CFFI_CTYPE_TAG;
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

static inline bool isctype(cdata const &cd) {
    return cd.gc_ref == lua::CFFI_CTYPE_TAG;
}

static inline cdata &checkcdata(lua_State *L, int idx) {
    auto ret = static_cast<cdata *>(
        luaL_checkudata(L, idx, lua::CFFI_CDATA_MT)
    );
    if (isctype(*ret)) {
        lua::type_error(L, idx, "cdata");
    }
    return *ret;
}

static inline cdata *testcval(lua_State *L, int idx) {
    return static_cast<cdata *>(
        luaL_testudata(L, idx, lua::CFFI_CDATA_MT)
    );
}

static inline cdata *testcdata(lua_State *L, int idx) {
    auto ret = static_cast<cdata *>(
        luaL_testudata(L, idx, lua::CFFI_CDATA_MT)
    );
    if (!ret || isctype(*ret)) {
        return nullptr;
    }
    return ret;
}

static inline cdata &tocdata(lua_State *L, int idx) {
    return *lua::touserdata<ffi::cdata>(L, idx);
}

/* careful with this; use only if you're sure you have cdata at the index */
static inline std::size_t cdata_value_size(lua_State *L, int idx) {
    auto &cd = tocdata(L, idx);
    if (cd.decl.vla()) {
        /* VLAs only exist on lua side, they are always allocated by us, so
         * we can be sure they are contained within the lua-allocated block
         *
         * the VLA memory consists of the following:
         * - the cdata sequence with overallocation padding
         * - the section where the pointer to data is stored
         * - and finally the VLA memory itself
         *
         * that means we take the length of the userdata and remove everything
         * that is not the raw array data, and that is our final length
         */
        return (
            lua_rawlen(L, idx) - cdata_pad_size() - sizeof(ffi::scalar_stor_t)
        );
    } else {
        /* otherwise the size is known, so fall back to that */
        return cd.decl.alloc_size();
    }
}

void destroy_cdata(lua_State *L, cdata &cd);
void destroy_closure(lua_State *L, closure_data *cd);

int call_cif(cdata &fud, lua_State *L, std::size_t largs);

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
    bool ffi_ret, bool lossy = false
);

/* a unified version of from_lua that combines together the complex aggregate
 * initialization logic and simple conversions from scalar types, resulting
 * in an all in one function that can take care of storing the C value of
 * a Lua value inside a piece of memory
 */
void from_lua(lua_State *L, ast::c_type const &decl, void *stor, int idx);

void get_global(lua_State *L, lib::c_lib const *dl, const char *sname);
void set_global(lua_State *L, lib::c_lib const *dl, char const *sname, int idx);

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
static inline bool test_arith(lua_State *L, int idx, T &out) {
    auto *cd = testcdata(L, idx);
    if (!cd) {
        if (util::is_int<T>::value) {
            if (lua_type(L, idx) == LUA_TNUMBER) {
                out = T(lua_tointeger(L, idx));
                return true;
            } else {
                return false;
            }
        }
        if (lua_type(L, idx) == LUA_TNUMBER) {
            out = T(lua_tointeger(L, idx));
            return true;
        }
        return false;
    }
    auto gf = [](int itp, void *av, T &rv) {
        switch (itp) {
            case ast::C_BUILTIN_ENUM:
                /* TODO: large enums */
                rv = T(*static_cast<int *>(av)); break;
            case ast::C_BUILTIN_BOOL:
                rv = T(*static_cast<bool *>(av)); break;
            case ast::C_BUILTIN_CHAR:
                rv = T(*static_cast<char *>(av)); break;
            case ast::C_BUILTIN_SCHAR:
                rv = T(*static_cast<signed char *>(av)); break;
            case ast::C_BUILTIN_UCHAR:
                rv = T(*static_cast<unsigned char *>(av)); break;
            case ast::C_BUILTIN_SHORT:
                rv = T(*static_cast<short *>(av)); break;
            case ast::C_BUILTIN_USHORT:
                rv = T(*static_cast<unsigned short *>(av)); break;
            case ast::C_BUILTIN_INT:
                rv = T(*static_cast<int *>(av)); break;
            case ast::C_BUILTIN_UINT:
                rv = T(*static_cast<unsigned int *>(av)); break;
            case ast::C_BUILTIN_LONG:
                rv = T(*static_cast<long *>(av)); break;
            case ast::C_BUILTIN_ULONG:
                rv = T(*static_cast<unsigned long *>(av)); break;
            case ast::C_BUILTIN_LLONG:
                rv = T(*static_cast<long long *>(av)); break;
            case ast::C_BUILTIN_ULLONG:
                rv = T(*static_cast<unsigned long long *>(av)); break;
            case ast::C_BUILTIN_FLOAT:
                rv = T(*static_cast<float *>(av)); break;
            case ast::C_BUILTIN_DOUBLE:
                rv = T(*static_cast<double *>(av)); break;
            case ast::C_BUILTIN_LDOUBLE:
                rv = T(*static_cast<long double *>(av)); break;
            default:
                return false;
        }
        return true;
    };
    int tp = cd->decl.type();
    if (cd->decl.is_ref()) {
        if (gf(tp, *static_cast<void **>(cd->as_ptr()), out)) {
            return true;
        }
    } else if (gf(tp, cd->as_ptr(), out)) {
        return true;
    }
    return false;
}

template<typename T>
static inline T check_arith(lua_State *L, int idx) {
    T outv{};
    if (!test_arith<T>(L, idx, outv)) {
        lua::type_error(
            L, idx, util::is_int<T>::value ? "integer" : "number"
        );
    }
    return outv;
}

static inline ast::c_expr_type check_arith_expr(
    lua_State *L, int idx, ast::c_value &iv
) {
    auto *cd = testcdata(L, idx);
    if (!cd) {
        /* some logic for conversions of lua numbers into cexprs */
#if LUA_VERSION_NUM >= 503
        static_assert(
            sizeof(lua_Integer) <= sizeof(long long),
            "invalid lua_Integer format"
        );
        if (lua_isinteger(L, idx)) {
            if (util::is_signed<lua_Integer>::value) {
                if (sizeof(lua_Integer) <= sizeof(int)) {
                    iv.i = int(lua_tointeger(L, idx));
                    return ast::c_expr_type::INT;
                } else if (sizeof(lua_Integer) <= sizeof(long)) {
                    iv.l = long(lua_tointeger(L, idx));
                    return ast::c_expr_type::LONG;
                } else {
                    using LL = long long;
                    iv.ll = LL(lua_tointeger(L, idx));
                    return ast::c_expr_type::LLONG;
                }
            } else {
                if (sizeof(lua_Integer) <= sizeof(unsigned int)) {
                    using U = unsigned int;
                    iv.u = U(lua_tointeger(L, idx));
                    return ast::c_expr_type::UINT;
                } else if (sizeof(lua_Integer) <= sizeof(unsigned long)) {
                    using UL = unsigned long;
                    iv.ul = UL(lua_tointeger(L, idx));
                    return ast::c_expr_type::ULONG;
                } else {
                    using ULL = unsigned long long;
                    iv.ull = ULL(lua_tointeger(L, idx));
                    return ast::c_expr_type::ULLONG;
                }
            }
        }
#endif
        static_assert(
            util::is_int<lua_Number>::value
                ? (sizeof(lua_Number) <= sizeof(long long))
                : (sizeof(lua_Number) <= sizeof(long double)),
            "invalid lua_Number format"
        );
        auto n = luaL_checknumber(L, idx);
        if (util::is_int<lua_Number>::value) {
            if (util::is_signed<lua_Number>::value) {
                if (sizeof(lua_Number) <= sizeof(int)) {
                    iv.i = int(n);
                    return ast::c_expr_type::INT;
                } else if (sizeof(lua_Number) <= sizeof(long)) {
                    iv.l = long(n);
                    return ast::c_expr_type::LONG;
                } else {
                    using LL = long long;
                    iv.ll = LL(n);
                    return ast::c_expr_type::LLONG;
                }
            } else {
                if (sizeof(lua_Number) <= sizeof(unsigned int)) {
                    using U = unsigned int;
                    iv.u = U(n);
                    return ast::c_expr_type::UINT;
                } else if (sizeof(lua_Number) <= sizeof(unsigned long)) {
                    using UL = unsigned long;
                    iv.ul = UL(n);
                    return ast::c_expr_type::ULONG;
                } else {
                    using ULL = unsigned long long;
                    iv.ull = ULL(n);
                    return ast::c_expr_type::ULLONG;
                }
            }
        } else if (sizeof(lua_Number) <= sizeof(float)) {
            iv.f = float(n);
            return ast::c_expr_type::FLOAT;
        } else if (sizeof(lua_Number) <= sizeof(double)) {
            iv.d = double(n);
            return ast::c_expr_type::DOUBLE;
        }
        using LD = long double;
        iv.ld = LD(n);
        return ast::c_expr_type::LDOUBLE;
    }
    auto gf = [](int itp, void *av, ast::c_value &v) {
        switch (itp) {
            case ast::C_BUILTIN_ENUM:
                /* TODO: large enums */
                v.i = *static_cast<int *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_BOOL:
                v.i = *static_cast<bool *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_CHAR:
                v.i = *static_cast<char *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_SCHAR:
                v.i = *static_cast<signed char *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_UCHAR:
                v.i = *static_cast<unsigned char *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_SHORT:
                v.i = *static_cast<short *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_USHORT:
                v.i = *static_cast<unsigned short *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_INT:
                v.i = *static_cast<int *>(av);
                return ast::c_expr_type::INT;
            case ast::C_BUILTIN_UINT:
                v.u = *static_cast<unsigned int *>(av);
                return ast::c_expr_type::UINT;
            case ast::C_BUILTIN_LONG:
                v.l = *static_cast<long *>(av);
                return ast::c_expr_type::LONG;
            case ast::C_BUILTIN_ULONG:
                v.ul = *static_cast<unsigned long *>(av);
                return ast::c_expr_type::ULONG;
            case ast::C_BUILTIN_LLONG:
                v.ll = *static_cast<long long *>(av);
                return ast::c_expr_type::LLONG;
            case ast::C_BUILTIN_ULLONG:
                v.ull = *static_cast<unsigned long long *>(av);
                return ast::c_expr_type::ULLONG;
            case ast::C_BUILTIN_FLOAT:
                v.f = *static_cast<float *>(av);
                return ast::c_expr_type::FLOAT;
            case ast::C_BUILTIN_DOUBLE:
                v.d = *static_cast<double *>(av);
                return ast::c_expr_type::DOUBLE;
            case ast::C_BUILTIN_LDOUBLE:
                v.ld = *static_cast<long double *>(av);
                return ast::c_expr_type::LDOUBLE;
            default:
                return ast::c_expr_type::INVALID;
        }
    };
    ast::c_expr_type ret;
    int tp = cd->decl.type();
    if (cd->decl.is_ref()) {
        ret = gf(tp, *static_cast<void **>(cd->as_ptr()), iv);
    } else {
        ret = gf(tp, cd->as_ptr(), iv);
    }
    if (ret == ast::c_expr_type::INVALID) {
        luaL_checknumber(L, idx);
    }
    /* unreachable */
    return ret;
}

static inline cdata &make_cdata_arith(
    lua_State *L, ast::c_expr_type et, ast::c_value const &cv
) {
    auto bt = ast::to_builtin_type(et);
    if (bt == ast::C_BUILTIN_INVALID) {
        luaL_error(L, "invalid value type");
    }
    auto tp = ast::c_type{bt, 0};
    auto as = tp.alloc_size();
    auto &cd = newcdata(L, util::move(tp), as);
    std::memcpy(cd.as_ptr(), &cv, as);
    return cd;
}

static inline char const *lua_serialize(lua_State *L, int idx) {
    auto *cd = testcdata(L, idx);
    if (cd) {
        /* it's ok to mess up the lua stack, this is only used for errors */
        cd->decl.serialize(L);
        return lua_tostring(L, -1);
    }
    return lua_typename(L, lua_type(L, idx));
}

} /* namespace ffi */

#endif /* FFI_HH */
