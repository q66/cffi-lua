#include <limits>
#include <type_traits>
#include <algorithm>

#include "platform.hh"
#include "ffi.hh"

namespace ffi {

static ffi_type *lua_to_vararg(lua_State *L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            return &ffi_type_uchar;
        case LUA_TNUMBER:
            /* 5.3+; always returns false on <= 5.2 */
            if (lua_isinteger(L, index)) {
                return ffi_traits<lua_Integer>::type();
            }
            return ffi_traits<lua_Number>::type();
        case LUA_TNIL:
        case LUA_TSTRING:
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TTHREAD:
        case LUA_TLIGHTUSERDATA:
            return &ffi_type_pointer;
        case LUA_TUSERDATA: {
            auto *cd = testcdata<noval>(L, index);
            /* plain userdata or struct values are passed to varargs as ptrs */
            if (!cd || (cd->decl.type() == ast::C_BUILTIN_RECORD)) {
                return &ffi_type_pointer;
            }
            return cd->decl.libffi_type();
        }
        default:
            break;
    }
    assert(false);
    return &ffi_type_void;
}

static inline void *fdata_retval(fdata &fd) {
    return &fd.rarg;
}

static inline arg_stor_t *&fdata_get_aux(fdata &fd) {
    union { arg_stor_t **np; arg_stor_t *op; } u;
    u.op = fd.args();
    return *u.np;
}

static inline void fdata_free_aux(fdata &fd) {
    auto &aux = fdata_get_aux(fd);
    delete[] reinterpret_cast<unsigned char *>(aux);
    aux = nullptr;
}

static inline void fdata_new_aux(fdata &fd, size_t sz) {
    fdata_get_aux(fd) = reinterpret_cast<arg_stor_t *>(new unsigned char[sz]);
}

static inline ffi_type **fargs_types(void *args, size_t nargs) {
    auto *bp = static_cast<arg_stor_t *>(args);
    return reinterpret_cast<ffi_type **>(&bp[nargs]);
}

static inline void **fargs_values(void *args, size_t nargs) {
    return reinterpret_cast<void **>(&fargs_types(args, nargs)[nargs]);
}

void destroy_cdata(lua_State *L, cdata<noval> &cd) {
    auto &fd = *reinterpret_cast<cdata<fdata> *>(&cd.decl);
    if (cd.gc_ref >= 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cd.gc_ref);
        lua_pushvalue(L, 1); /* the cdata */
        if (lua_pcall(L, 1, 0, 0)) {
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, cd.gc_ref);
    }
    if (cd.decl.closure() && fd.val.cd) {
        /* this is O(n) which sucks a little */
        fd.val.cd->refs.remove(&fd.val.cd);
    }
    switch (cd.decl.type()) {
        case ast::C_BUILTIN_PTR:
            if (cd.decl.ptr_base().type() != ast::C_BUILTIN_FUNC) {
                break;
            }
            goto free_aux;
        free_aux:
        case ast::C_BUILTIN_FUNC: {
            if (!fd.decl.function().variadic()) {
                break;
            }
            fdata_free_aux(fd.val);
        }
        default:
            break;
    }
    using T = ast::c_type;
    cd.decl.~T();
}

void destroy_closure(closure_data *cd) {
    cd->~closure_data();
    delete[] reinterpret_cast<unsigned char *>(cd);
}

static void cb_bind(ffi_cif *, void *ret, void *args[], void *data) {
    auto &fud = *static_cast<cdata<fdata> *>(data);
    auto &fun = fud.decl.function();
    auto &pars = fun.params();
    size_t fargs = pars.size();

    closure_data &cd = *fud.val.cd;
    lua_rawgeti(cd.L, LUA_REGISTRYINDEX, cd.fref);
    for (size_t i = 0; i < fargs; ++i) {
        to_lua(cd.L, pars[i].type(), args[i], RULE_PASS);
    }

    if (fun.result().type() != ast::C_BUILTIN_VOID) {
        lua_call(cd.L, fargs, 1);
        arg_stor_t stor;
        size_t rsz;
        void *rp = from_lua(
            cd.L, fun.result(), &stor, -1, rsz, RULE_RET
        );
        memcpy(ret, rp, rsz);
        lua_pop(cd.L, 1);
    } else {
        lua_call(cd.L, fargs, 0);
    }
}

/* this initializes a non-vararg cif with the given number of arguments
 * for variadics, this is initialized once for zero args, and then handled
 * dynamically before every call
 */
static bool prepare_cif(
    ast::c_function const &func, ffi_cif &cif, ffi_type **targs, size_t nargs
) {
    for (size_t i = 0; i < nargs; ++i) {
        targs[i] = func.params()[i].libffi_type();
    }
    return (ffi_prep_cif(
        &cif, FFI_DEFAULT_ABI, nargs,
        func.result().libffi_type(), targs
    ) == FFI_OK);
}

static void make_cdata_func(
    lua_State *L, void (*funp)(), ast::c_function const &func, bool fptr,
    closure_data *cd
) {
    size_t nargs = func.params().size();

    /* MEMORY LAYOUT:
     *
     * regular func:
     *
     * struct cdata {
     *     <cdata header>
     *     struct fdata {
     *         <fdata header>
     *         arg_stor_t val1; // lua arg1
     *         arg_stor_t val2; // lua arg2
     *         arg_stor_t valN; // lua argN
     *         ffi_type *arg1; // type
     *         ffi_type *arg2; // type
     *         ffi_type *argN; // type
     *         void *valp1;    // &val1
     *         void *valpN;    // &val2
     *         void *valpN;    // &valN
     *     } val;
     * }
     *
     * vararg func:
     *
     * struct cdata {
     *     <cdata header>
     *     struct fdata {
     *         <fdata header>
     *         void *aux; // vals + types + args like above, but dynamic
     *     } val;
     * }
     */
    ast::c_type funct{&func, 0, funp == nullptr};
    auto &fud = newcdata<fdata>(
        L, fptr ? ast::c_type{std::move(funct), 0} : std::move(funct),
        func.variadic() ? sizeof(void *) : (
            sizeof(arg_stor_t) * nargs + sizeof(void *) * nargs * 2
        )
    );
    fud.val.sym = funp;

    if (func.variadic()) {
        fdata_get_aux(fud.val) = nullptr;
        if (!funp) {
            luaL_error(L, "variadic callbacks are not supported");
        }
        nargs = 0;
    }

    if (!prepare_cif(
        func, fud.val.cif, fargs_types(fud.val.args(), nargs), nargs
    )) {
        luaL_error(L, "unexpected failure setting up '%s'", func.name());
    }

    if (!funp) {
        /* no funcptr means we're setting up a callback */
        if (cd) {
            /* copying existing callback reference */
            cd->refs.push_front(&fud.val.cd);
            fud.val.cd = cd;
            return;
        }
        cd = reinterpret_cast<closure_data *>(new unsigned char[
            sizeof(closure_data) + nargs * sizeof(ffi_type *)
        ]);
        new (cd) closure_data{};
        /* allocate a closure in it */
        cd->closure = static_cast<ffi_closure *>(ffi_closure_alloc(
            sizeof(ffi_closure), reinterpret_cast<void **>(&fud.val.sym)
        ));
        if (!cd->closure) {
            destroy_closure(cd);
            luaL_error(
                L, "failed allocating callback for '%s'",
                func.serialize().c_str()
            );
        }
        if (!prepare_cif(fud.decl.function(), cd->cif, cd->targs(), nargs)) {
            destroy_closure(cd);
            luaL_error(L, "unexpected failure setting up '%s'", func.name());
        }
        if (ffi_prep_closure_loc(
            cd->closure, &fud.val.cif, cb_bind, &fud,
            reinterpret_cast<void *>(fud.val.sym)
        ) != FFI_OK) {
            destroy_closure(cd);
            luaL_error(
                L, "failed initializing closure for '%s'",
                func.serialize().c_str()
            );
        }
        cd->L = L;
        /* register this reference within the closure */
        cd->refs.push_front(&fud.val.cd);
        fud.val.cd = cd;
    }
}

static bool prepare_cif_var(
    lua_State *L, cdata<fdata> &fud, size_t nargs, size_t fargs
) {
    auto &func = fud.decl.function();

    auto &auxptr = fdata_get_aux(fud.val);
    if (auxptr && (nargs > size_t(fud.aux))) {
        fdata_free_aux(fud.val);
    }
    if (!auxptr) {
        fdata_new_aux(
            fud.val, nargs * sizeof(arg_stor_t) + 2 * nargs * sizeof(void *)
        );
        fud.aux = int(nargs);
    }

    ffi_type **targs = fargs_types(auxptr, nargs);
    for (size_t i = 0; i < fargs; ++i) {
        targs[i] = func.params()[i].libffi_type();
    }
    for (size_t i = fargs; i < nargs; ++i) {
        targs[i] = lua_to_vararg(L, i + 2);
    }

    return (ffi_prep_cif_var(
        &fud.val.cif, FFI_DEFAULT_ABI, fargs, nargs,
        func.result().libffi_type(), targs
    ) == FFI_OK);
}

int call_cif(cdata<fdata> &fud, lua_State *L, size_t largs) {
    auto &func = fud.decl.function();
    auto &pdecls = func.params();

    size_t nargs = pdecls.size();
    size_t targs = nargs;

    arg_stor_t *pvals = fud.val.args();
    void *rval = fdata_retval(fud.val);

    if (func.variadic()) {
        targs = std::max(largs, nargs);
        if (!prepare_cif_var(L, fud, targs, nargs)) {
            luaL_error(L, "unexpected failure setting up '%s'", func.name());
        }
        pvals = fdata_get_aux(fud.val);
    }

    void **vals = fargs_values(pvals, targs);
    /* fixed args */
    for (size_t i = 0; i < nargs; ++i) {
        size_t rsz;
        vals[i] = from_lua(
            L, pdecls[i].type(), &pvals[i], i + 2, rsz, RULE_PASS
        );
    }
    /* variable args */
    for (size_t i = nargs; i < targs; ++i) {
        size_t rsz;
        auto tp = ast::from_lua_type(L, i + 2);
        if (tp.type() == ast::C_BUILTIN_RECORD) {
            /* special case for vararg passing of records: by ptr */
            auto &cd = tocdata<void *>(L, i + 2);
            memcpy(&pvals[i], &cd.val, sizeof(void *));
            continue;
        }
        vals[i] = from_lua(L, std::move(tp), &pvals[i], i + 2, rsz, RULE_PASS);
    }

    ffi_call(&fud.val.cif, fud.val.sym, rval, vals);
#ifdef FFI_BIG_ENDIAN
    /* for small return types, ffi_arg must be used to hold the result,
     * and it is assumed that they will be accessed like integers via
     * the ffi_arg; that also means that on big endian systems the
     * value will be stored in the latter part of the memory...
     *
     * we're taking an address to the beginning in general, so make
     * a special case here; only small types will have this problem
     *
     * there shouldn't be any other places that make this assumption
     */
    auto rsz = func.result().alloc_size();
    if (rsz < sizeof(ffi_arg)) {
        auto *p = static_cast<unsigned char *>(rval);
        rval = p + sizeof(ffi_arg) - rsz;
    }
#endif
    return to_lua(L, func.result(), rval, RULE_RET);
}

template<typename T>
static inline int push_int(
    lua_State *L, ast::c_type const &tp, void const *value, bool lossy
) {
#if LUA_VERSION_NUM < 503
    /* generally floats, so we're assuming IEEE754 binary floats */
    static_assert(
        std::numeric_limits<lua_Number>::radix == std::numeric_limits<T>::radix,
        "type radix differs"
    );
    using LT = lua_Number;
#else
    /* on lua 5.3+, we can use integers builtin in the language instead */
    using LT = lua_Integer;
#endif
    if ((
        std::numeric_limits<T>::digits <= std::numeric_limits<LT>::digits
    ) || lossy) {
        using U = T *;
        lua_pushinteger(L, lua_Integer(*U(value)));
        return 1;
    }
    /* doesn't fit into the range, so make scalar cdata */
    auto &cd = newcdata(L, tp, sizeof(T));
    memcpy(&cd.val, value, sizeof(T));
    return 1;
}

template<typename T>
static inline int push_flt(
    lua_State *L, ast::c_type const &tp, void const *value, bool lossy
) {
    /* probably not the best check */
    if ((
        std::numeric_limits<T>::max() <=
        std::numeric_limits<lua_Number>::max()
    ) || lossy) {
        using U = T *;
        lua_pushnumber(L, lua_Number(*U(value)));
        return 1;
    }
    auto &cd = newcdata(L, tp, sizeof(T));
    memcpy(&cd.val, value, sizeof(T));
    return 1;
}

int to_lua(
    lua_State *L, ast::c_type const &tp, void const *value,
    int rule, bool lossy
) {
    switch (ast::c_builtin(tp.type())) {
        /* no retval */
        case ast::C_BUILTIN_VOID:
            return 0;
        /* convert to lua boolean */
        case ast::C_BUILTIN_BOOL:
            lua_pushboolean(L, *static_cast<bool const *>(value));
            return 1;
        /* convert to lua number */
        case ast::C_BUILTIN_FLOAT:
            return push_flt<float>(L, tp, value, lossy);
        case ast::C_BUILTIN_DOUBLE:
            return push_flt<double>(L, tp, value, lossy);
        case ast::C_BUILTIN_LDOUBLE:
            return push_flt<long double>(L, tp, value, lossy);
        case ast::C_BUILTIN_CHAR:
            return push_int<char>(L, tp, value, lossy);
        case ast::C_BUILTIN_SCHAR:
            return push_int<signed char>(L, tp, value, lossy);
        case ast::C_BUILTIN_UCHAR:
            return push_int<unsigned char>(L, tp, value, lossy);
        case ast::C_BUILTIN_SHORT:
            return push_int<short>(L, tp, value, lossy);
        case ast::C_BUILTIN_USHORT:
            return push_int<unsigned short>(L, tp, value, lossy);
        case ast::C_BUILTIN_INT:
            return push_int<int>(L, tp, value, lossy);
        case ast::C_BUILTIN_UINT:
            return push_int<unsigned int>(L, tp, value, lossy);
        case ast::C_BUILTIN_LONG:
            return push_int<long>(L, tp, value, lossy);
        case ast::C_BUILTIN_ULONG:
            return push_int<unsigned long>(L, tp, value, lossy);
        case ast::C_BUILTIN_LLONG:
            return push_int<long long>(L, tp, value, lossy);
        case ast::C_BUILTIN_ULLONG:
            return push_int<unsigned long long>(L, tp, value, lossy);

        case ast::C_BUILTIN_REF:
            if (rule == RULE_CONV) {
                /* for this rule, dereference and pass that */
                if (tp.ptr_base().type() == ast::C_BUILTIN_FUNC) {
                    make_cdata_func(
                        L, *reinterpret_cast<void (* const *)()>(value),
                        tp.function(), false, nullptr
                    );
                    return 1;
                }
                return to_lua(
                    L, tp.ptr_base(), *reinterpret_cast<void * const *>(value),
                    RULE_CONV, lossy
                );
            }
            goto ptr_ref;

        ptr_ref:
        case ast::C_BUILTIN_PTR:
            if (tp.ptr_base().type() == ast::C_BUILTIN_FUNC) {
                return to_lua(L, tp.ptr_base(), value, rule, lossy);
            }
            /* pointers should be handled like large cdata, as they need
             * to be represented as userdata objects on lua side either way
             */
            newcdata<void *>(L, tp).val =
                *reinterpret_cast<void * const *>(value);
            return 1;

        case ast::C_BUILTIN_VA_LIST:
            newcdata<void *>(L, tp).val =
                *reinterpret_cast<void * const *>(value);
            return 1;

        case ast::C_BUILTIN_FUNC:
            make_cdata_func(
                L, *reinterpret_cast<void (* const *)()>(value),
                tp.function(), true, nullptr
            );
            return 1;

        case ast::C_BUILTIN_ENUM:
            /* TODO: large enums */
            return push_int<int>(L, tp, value, lossy);

        case ast::C_BUILTIN_ARRAY: {
            if (rule == RULE_CONV) {
                /* here, value may be a pointer to temporary, hack around it */
                auto &cd = newcdata<void *[2]>(L, ast::c_type{
                    tp, 0, ast::C_BUILTIN_REF
                });
                cd.val[1] = *reinterpret_cast<void * const *>(value);
                cd.val[0] = &cd.val[1];
                return 1;
            }
            newcdata<void *>(L, tp).val =
                *reinterpret_cast<void * const *>(value);
            return 1;
        }

        case ast::C_BUILTIN_RECORD: {
            if (rule == RULE_CONV) {
                newcdata<void const *>(
                    L, ast::c_type{tp, 0, ast::C_BUILTIN_REF}
                ).val = value;
                return 1;
            }
            auto sz = tp.alloc_size();
            auto &cd = newcdata(L, tp, sz);
            memcpy(&cd.val, value, sz);
            return 1;
        }

        case ast::C_BUILTIN_INVALID:
            break;
    }

    luaL_error(L, "unexpected error: unhandled type %d", tp.type());
    return 0;
}

template<typename T>
static inline void *write_int(lua_State *L, int index, void *stor, size_t &s) {
    lua_Integer v = lua_isboolean(L, index) ?
        lua_toboolean(L, index) : lua_tointeger(L, index);
    *static_cast<T *>(stor) = T(v);
    s = sizeof(T);
    return stor;
}

template<typename T>
static inline void *write_flt(lua_State *L, int index, void *stor, size_t &s) {
    lua_Number v = lua_isboolean(L, index) ?
        lua_toboolean(L, index) : lua_tonumber(L, index);
    *static_cast<T *>(stor) = T(v);
    s = sizeof(T);
    return stor;
}

static void *from_lua_num(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    size_t &dsz, int rule
) {
    switch (ast::c_builtin(tp.type())) {
        case ast::C_BUILTIN_FLOAT:
            return write_flt<float>(L, index, stor, dsz);
        case ast::C_BUILTIN_DOUBLE:
            return write_flt<double>(L, index, stor, dsz);
        case ast::C_BUILTIN_LDOUBLE:
            return write_flt<long double>(L, index, stor, dsz);
        case ast::C_BUILTIN_BOOL:
            return write_int<bool>(L, index, stor, dsz);
        case ast::C_BUILTIN_CHAR:
            return write_int<char>(L, index, stor, dsz);
        case ast::C_BUILTIN_SCHAR:
            return write_int<signed char>(L, index, stor, dsz);
        case ast::C_BUILTIN_UCHAR:
            return write_int<unsigned char>(L, index, stor, dsz);
        case ast::C_BUILTIN_SHORT:
            return write_int<short>(L, index, stor, dsz);
        case ast::C_BUILTIN_USHORT:
            return write_int<unsigned short>(L, index, stor, dsz);
        case ast::C_BUILTIN_INT:
            return write_int<int>(L, index, stor, dsz);
        case ast::C_BUILTIN_UINT:
            return write_int<unsigned int>(L, index, stor, dsz);
        case ast::C_BUILTIN_LONG:
            return write_int<long>(L, index, stor, dsz);
        case ast::C_BUILTIN_ULONG:
            return write_int<unsigned long>(L, index, stor, dsz);
        case ast::C_BUILTIN_LLONG:
            return write_int<long long>(L, index, stor, dsz);
        case ast::C_BUILTIN_ULLONG:
            return write_int<unsigned long long>(
                L, index, stor, dsz
            );

        case ast::C_BUILTIN_ENUM:
            /* TODO: large enums */
            return write_int<int>(L, index, stor, dsz);

        case ast::C_BUILTIN_PTR:
        case ast::C_BUILTIN_REF:
            if (rule == RULE_CAST) {
                dsz = sizeof(void *);
                return &(*static_cast<void **>(stor) = reinterpret_cast<void *>(
                    size_t(lua_tointeger(L, index))
                ));
            }
            goto converr;

        converr:
        case ast::C_BUILTIN_VOID:
        case ast::C_BUILTIN_RECORD:
        case ast::C_BUILTIN_ARRAY:
        case ast::C_BUILTIN_VA_LIST:
            luaL_error(
                L, "cannot convert '%s' to '%s'",
                lua_typename(L, lua_type(L, index)),
                tp.serialize().c_str()
            );
            break;

        case ast::C_BUILTIN_FUNC:
        case ast::C_BUILTIN_INVALID:
            /* this should not happen */
            luaL_error(
                L, "bad argument type '%s'", tp.serialize().c_str()
            );
            break;
    }
    assert(false);
    return nullptr;
}

static inline bool cv_convertible(int scv, int tcv) {
    if (!(tcv & ast::C_CV_CONST) && (scv & ast::C_CV_CONST)) {
        return false;
    }
    if (!(tcv & ast::C_CV_VOLATILE) && (scv & ast::C_CV_VOLATILE)) {
        return false;
    }
    return true;
}

static inline bool func_convertible(
    ast::c_function const &from, ast::c_function const &to
) {
    if (from.variadic() != to.variadic()) {
        return false;
    }
    if (from.params().size() != to.params().size()) {
        return false;
    }
    return true;
}

static inline bool ptr_convertible(
    ast::c_type const &from, ast::c_type const &to
) {
    auto &fpb = from.ptr_base();
    auto &tpb = to.ptr_base();
    if (!cv_convertible(fpb.cv(), tpb.cv())) {
        return false;
    }
    if (
        (fpb.type() == ast::C_BUILTIN_VOID) ||
        (tpb.type() == ast::C_BUILTIN_VOID)
    ) {
        /* from or to void pointer is always ok */
        return true;
    }
    if (
        (fpb.type() == ast::C_BUILTIN_PTR) &&
        (tpb.type() == ast::C_BUILTIN_PTR)
    ) {
        return ptr_convertible(fpb, tpb);
    }
    return fpb.is_same(tpb, true);
}

static inline void fail_convert_cd(
    lua_State *L, ast::c_type const &from, ast::c_type const &to
) {
    luaL_error(
        L, "cannot convert '%s' to '%s'",
        from.serialize().c_str(), to.serialize().c_str()
    );
}

/* converting from cdata: pointer */
static void from_lua_cdata_ptr(
    lua_State *L, ast::c_type const &cd, ast::c_type const &tp, int rule
) {
    switch (tp.type()) {
        /* converting to pointer or reference */
        case ast::C_BUILTIN_PTR:
        case ast::C_BUILTIN_REF: {
            if (rule == RULE_CAST) {
                /* casting: disregard any typing rules */
                return;
            }
            /* initializing a function pointer/reference */
            if (tp.ptr_base().type() == ast::C_BUILTIN_FUNC) {
                if (cd.type() == ast::C_BUILTIN_FUNC) {
                    /* plain function: check convertible, init from addr */
                    if (!func_convertible(
                        cd.function(), tp.ptr_base().function()
                    )) {
                        fail_convert_cd(L, cd, tp);
                    }
                    return;
                } else if (
                    (cd.type() != ast::C_BUILTIN_PTR) &&
                    (cd.type() != ast::C_BUILTIN_REF)
                ) {
                    /* otherwise given value must be a pointer/ref */
                    fail_convert_cd(L, cd, tp);
                }
                /* it must be a pointer/ref to function */
                if (cd.ptr_base().type() != ast::C_BUILTIN_FUNC) {
                    fail_convert_cd(L, cd, tp);
                }
                /* and it must satisfy convertible check */
                if (!func_convertible(
                    cd.ptr_base().function(), tp.ptr_base().function()
                )) {
                    fail_convert_cd(L, cd, tp);
                }
                /* then init from address */
                return;
            }
            if (!ptr_convertible(cd, tp)) {
                fail_convert_cd(L, cd, tp);
            }
            return;
        }
        /* converting to anything else */
        default:
            break;
    }
    fail_convert_cd(L, cd, tp);
}

template<typename T>
static void *from_lua_cnumber(
    lua_State *L, ast::c_type const &cd, ast::c_type const &tp,
    void *sval, void *stor, size_t &dsz, int rule
) {
#define CONV_CASE(name, U) \
    case ast::C_BUILTIN_##name: \
        dsz = sizeof(U); \
        return &(*static_cast<U *>(stor) = *static_cast<T *>(sval));

    switch (tp.type()) {
        case ast::C_BUILTIN_PTR:
        case ast::C_BUILTIN_REF:
            /* only for cast we can initialize pointers with integer addrs */
            if (rule != RULE_CAST) {
                break;
            }
            /* must not be floating point */
            if (!std::is_integral<T>::value) {
                break;
            }
            dsz = sizeof(void *);
            return &(*static_cast<void **>(stor) = reinterpret_cast<void *>(
                size_t(*static_cast<T *>(sval))
            ));
        CONV_CASE(ENUM, int)
        CONV_CASE(BOOL, bool)
        CONV_CASE(CHAR, char)
        CONV_CASE(SCHAR, signed char)
        CONV_CASE(UCHAR, unsigned char)
        CONV_CASE(SHORT, short)
        CONV_CASE(USHORT, unsigned short)
        CONV_CASE(INT, int)
        CONV_CASE(UINT, unsigned int)
        CONV_CASE(LONG, long)
        CONV_CASE(ULONG, unsigned long)
        CONV_CASE(LLONG, long long)
        CONV_CASE(ULLONG, unsigned long long)
        CONV_CASE(FLOAT, float)
        CONV_CASE(DOUBLE, double)
        CONV_CASE(LDOUBLE, long double)
        default:
            break;
    }

#undef CONV_CASE

    luaL_error(
        L, "cannot convert '%s' to '%s'",
        cd.serialize().c_str(),
        tp.serialize().c_str()
    );
    return nullptr;
}

static void *from_lua_cdata(
    lua_State *L, ast::c_type const &cd, ast::c_type const &tp, void *sval,
    void *stor, size_t &dsz, int rule
) {
    /* arrays always decay to pointers first */
    if (cd.type() == ast::C_BUILTIN_ARRAY) {
        return from_lua_cdata(
            L, cd.as_type(ast::C_BUILTIN_PTR), tp, sval, stor, dsz, rule
        );
    }
    /* we're passing an argument and the expected type is a reference...
     * this is a special case, the given type must be either a non-reference
     * type that matches the base type of the reference and has same or weaker
     * qualifiers - then its address is taken - or a matching reference type,
     * then it's passed as-is
     */
    if ((rule == RULE_PASS) && (tp.type() == ast::C_BUILTIN_REF)) {
        if (cd.type() == ast::C_BUILTIN_REF) {
            return from_lua_cdata(
                L, cd.ptr_base(), tp, *static_cast<void **>(sval),
                stor, dsz, rule
            );
        }
        if (!cv_convertible(cd.cv(), tp.ptr_base().cv())) {
            fail_convert_cd(L, cd, tp);
        }
        if (!cd.is_same(tp.ptr_base(), true)) {
            fail_convert_cd(L, cd, tp);
        }
        dsz = sizeof(void *);
        return &(*static_cast<void **>(stor) = sval);
    }
    switch (cd.type()) {
        case ast::C_BUILTIN_PTR:
            from_lua_cdata_ptr(L, cd, tp, rule);
            dsz = sizeof(void *);
            return sval;
        case ast::C_BUILTIN_FUNC:
            if (
                (tp.type() != ast::C_BUILTIN_PTR) &&
                (tp.type() != ast::C_BUILTIN_REF)
            ) {
                /* converting from func: must be to some kind of pointer */
                fail_convert_cd(L, cd, tp);
            }
            if (rule == RULE_CAST) {
                /* casting: ignore rules, convert to any pointer */
                dsz = sizeof(void *);
                return sval;
            }
            /* not casting: some rules must be followed */
            if (tp.ptr_base().type() != ast::C_BUILTIN_FUNC) {
                fail_convert_cd(L, cd, tp);
            }
            if (!func_convertible(cd.function(), tp.ptr_base().function())) {
                fail_convert_cd(L, cd, tp);
            }
            dsz = sizeof(void *);
            return sval;
        case ast::C_BUILTIN_REF:
            /* always dereference */
            return from_lua_cdata(
                L, cd.ptr_base(), tp, *static_cast<void **>(sval),
                stor, dsz, rule
            );
        case ast::C_BUILTIN_RECORD:
            /* we can initialize pointers and references by address */
            if (
                (tp.type() != ast::C_BUILTIN_PTR) &&
                (tp.type() != ast::C_BUILTIN_REF)
            ) {
                break;
            }
            if (rule != RULE_CAST) {
                if (!cv_convertible(cd.cv(), tp.ptr_base().cv())) {
                    fail_convert_cd(L, cd, tp);
                }
            }
            dsz = sizeof(void *);
            return &(*static_cast<void **>(stor) = sval);
        default:
            if (cd.is_same(tp, true)) {
                dsz = cd.alloc_size();
                return sval;
            }
            break;
    }

#define CONV_CASE(name, T) \
    case ast::C_BUILTIN_##name: \
        return from_lua_cnumber<T>(L, cd, tp, sval, stor, dsz, rule);

    switch (cd.type()) {
        CONV_CASE(ENUM, int)
        CONV_CASE(BOOL, bool)
        CONV_CASE(CHAR, char)
        CONV_CASE(SCHAR, signed char)
        CONV_CASE(UCHAR, unsigned char)
        CONV_CASE(SHORT, short)
        CONV_CASE(USHORT, unsigned short)
        CONV_CASE(INT, int)
        CONV_CASE(UINT, unsigned int)
        CONV_CASE(LONG, long)
        CONV_CASE(ULONG, unsigned long)
        CONV_CASE(LLONG, long long)
        CONV_CASE(ULLONG, unsigned long long)
        CONV_CASE(FLOAT, float)
        CONV_CASE(DOUBLE, double)
        CONV_CASE(LDOUBLE, long double)
        default:
            break;
    }

#undef CONV_CASE

    luaL_error(
        L, "cannot convert '%s' to '%s'",
        cd.serialize().c_str(),
        tp.serialize().c_str()
    );
    return nullptr;
}

void *from_lua(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    size_t &dsz, int rule
) {
    /* sanitize the output type early on */
    switch (tp.type()) {
        case ast::C_BUILTIN_FUNC:
        case ast::C_BUILTIN_VOID:
        case ast::C_BUILTIN_INVALID:
            luaL_error(L, "invalid C type");
            break;
        case ast::C_BUILTIN_ARRAY:
            /* special cased for passing because those are passed by ptr */
            if (rule != RULE_PASS) {
                luaL_error(L, "invalid C type");
            }
            break;
        case ast::C_BUILTIN_RECORD:
            /* structs can be copied via new/pass but not casted */
            if (rule == RULE_CAST) {
                luaL_error(L, "invalid C type");
            }
        default:
            break;
    }
    auto vtp = lua_type(L, index);
    switch (vtp) {
        case LUA_TNIL:
            switch (tp.type()) {
                case ast::C_BUILTIN_REF:
                    if (rule == RULE_CAST) {
                        goto likeptr;
                    }
                    break;
                likeptr:
                case ast::C_BUILTIN_PTR:
                    dsz = sizeof(void *);
                    return &(*static_cast<void **>(stor) = nullptr);
                default:
                    break;
            }
            luaL_error(
                L, "cannot convert 'nil' to '%s'",
                tp.serialize().c_str()
            );
            break;
        case LUA_TNUMBER:
        case LUA_TBOOLEAN:
            return from_lua_num(L, tp, stor, index, dsz, rule);
            break;
        case LUA_TSTRING:
            if ((rule == RULE_CAST) || (
                (tp.type() == ast::C_BUILTIN_PTR) &&
                (tp.ptr_base().type() == ast::C_BUILTIN_CHAR) &&
                (tp.ptr_base().cv() & ast::C_CV_CONST)
            )) {
                dsz = sizeof(char const *);
                return &(
                    *static_cast<char const **>(stor) = lua_tostring(L, index)
                );
            }
            luaL_error(
                L, "cannot convert 'string' to '%s'", tp.serialize().c_str()
            );
            break;
        case LUA_TUSERDATA: {
            if (iscdata(L, index)) {
                auto &cd = *lua::touserdata<cdata<void *>>(L, index);
                return from_lua_cdata(
                    L, cd.decl, tp, &cd.val, stor, dsz, rule
                );
            }
            auto tpt = tp.type();
            if (tpt == ast::C_BUILTIN_PTR) {
                dsz = sizeof(void *);
                /* special handling for FILE handles */
                void *ud = lua_touserdata(L, index);
                if (luaL_testudata(L, index, LUA_FILEHANDLE)) {
                    FILE **f = static_cast<FILE **>(ud);
                    return &(*static_cast<void **>(stor) = *f);
                }
                /* other userdata: convert to any pointer when
                 * casting, otherwise only to a void pointer
                 */
                if (
                    (rule == RULE_CAST) ||
                    (tp.ptr_base().type() == ast::C_BUILTIN_VOID)
                ) {
                    return &(*static_cast<void **>(stor) = ud);
                }
            } else if ((tpt == ast::C_BUILTIN_REF) && (rule == RULE_CAST)) {
                /* when casting we can initialize refs from userdata */
                void *ud = lua_touserdata(L, index);
                if (luaL_testudata(L, index, LUA_FILEHANDLE)) {
                    FILE **f = static_cast<FILE **>(ud);
                    return &(*static_cast<void **>(stor) = *f);
                }
                return &(*static_cast<void **>(stor) = ud);
            }
            /* error in other cases */
            if (isctype(L, index)) {
                luaL_error(
                    L, "cannot convert 'ctype' to '%s'",
                    tp.serialize().c_str()
                );
            } else {
                luaL_error(
                    L, "cannot convert 'userdata' to '%s'",
                    tp.serialize().c_str()
                );
            }
            break;
        }
        case LUA_TLIGHTUSERDATA:
            if (tp.type() == ast::C_BUILTIN_PTR) {
                dsz = sizeof(void *);
                return &(
                    *static_cast<void **>(stor) = lua_touserdata(L, index)
                );
            } else {
                luaL_error(
                    L, "cannot convert 'lightuserdata' to '%s'",
                    tp.serialize().c_str()
                );
            }
            break;
        case LUA_TTABLE:
            /* we can't handle table initializers here because the memory
             * for the new cdata doesn't exist yet by this point...
             *
             * but that is only supported for ffi.new, and everything else
             * should error anyway, so do so here
             */
            luaL_error(
                L, "cannot convert 'table' to '%s'",
                tp.serialize().c_str()
            );
            break;
        case LUA_TFUNCTION:
            if (!tp.callable()) {
                luaL_error(
                    L, "cannot convert 'function' to '%s'",
                    tp.serialize().c_str()
                );
            }
            lua_pushvalue(L, index);
            *static_cast<int *>(stor) = luaL_ref(L, LUA_REGISTRYINDEX);
            /* we don't have a value to store */
            return nullptr;
        default:
            luaL_error(
                L, "cannot convert '%s' to '%s'",
                lua_typename(L, lua_type(L, index)), tp.serialize().c_str()
            );
            break;
    }
    assert(false);
    return nullptr;
}

static inline void push_init(lua_State *L, int tidx, int iidx) {
    if (!tidx) {
        lua_pushvalue(L, iidx);
    } else {
        lua_rawgeti(L, tidx, iidx);
    }
}

static inline int get_init_sidx(lua_State *L, int tidx, int &ninit) {
    auto rl = lua_rawlen(L, tidx);
    lua_rawgeti(L, tidx, 0);
    int sidx = 1;
    if (!lua_isnil(L, -1)) {
        ++rl;
        sidx = 0;
    }
    lua_pop(L, 1);
    if (rl > 0) {
        ninit = int(rl);
        return sidx;
    }
    ninit = -1;
    return -1;
}

static void from_lua_table(
    lua_State *L, ast::c_type const &decl, void *stor, size_t rsz,
    int tidx, int sidx, int ninit
);

static void from_lua_table_record(
    lua_State *L, ast::c_type const &decl, void *stor, size_t rsz,
    int tidx, int sidx, int ninit
) {
    auto &sb = decl.record();
    if (sb.fields().empty()) {
        return;
    }
    bool uni = sb.is_union();
    auto *val = static_cast<unsigned char *>(stor);
    bool filled = false;
    sb.iter_fields([L, rsz, val, &decl, &sidx, &filled, &ninit, tidx, uni](
        char const *fname, ast::c_type const &fld, size_t off
    ) {
        if (tidx && (ninit < 0)) {
            lua_getfield(L, tidx, fname);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                return uni || fld.unbounded();
            }
        } else if (ninit) {
            push_init(L, tidx, sidx++);
            --ninit;
        } else {
            /* no more items to initialize */
            return true;
        }
        /* flex array members */
        if (!uni && fld.unbounded()) {
            /* the size of the struct minus the flex member plus padding */
            size_t ssz = decl.alloc_size();
            /* initialize the last part as in array */
            size_t asz = rsz - ssz;
            if (lua_istable(L, -1)) {
                int ntidx = lua_gettop(L);
                int nninit;
                int nsidx = get_init_sidx(L, ntidx, nninit);
                from_lua_table(L, fld, &val[ssz], asz, ntidx, nsidx, nninit);
                lua_pop(L, 1);
            } else if (!ninit) {
                from_lua_table(L, fld, &val[ssz], asz, 0, lua_gettop(L), 1);
                lua_pop(L, 1);
            } else {
                lua_pop(L, 1);
                from_lua_table(
                    L, fld, &val[ssz], asz, tidx, sidx - 1, ninit + 1
                );
            }
            return true;
        }
        bool elem_struct = (fld.type() == ast::C_BUILTIN_RECORD);
        bool elem_arr = (fld.type() == ast::C_BUILTIN_ARRAY);
        if ((elem_arr || elem_struct) && lua_istable(L, -1)) {
            int ntidx = lua_gettop(L);
            int nninit;
            int nsidx = get_init_sidx(L, ntidx, nninit);
            from_lua_table(
                L, fld, &val[off], fld.alloc_size(), ntidx, nsidx, nninit
            );
        } else {
            size_t esz;
            arg_stor_t sv{};
            void *ep = from_lua(L, fld, &sv, -1, esz, RULE_CONV);
            memcpy(&val[off], ep, esz);
        }
        filled = true;
        lua_pop(L, 1);
        return uni;
    });
    if (uni && !filled) {
        memset(stor, 0, rsz);
    }
}

/* this can't be done in from_lua, because when from_lua is called, the
 * memory is not allocated yet... so do it here, as a special case
 */
static void from_lua_table(
    lua_State *L, ast::c_type const &decl, void *stor, size_t rsz,
    int tidx, int sidx, int ninit
) {
    if (decl.type() == ast::C_BUILTIN_RECORD) {
        from_lua_table_record(L, decl, stor, rsz, tidx, sidx, ninit);
        return;
    }

    if (ninit <= 0) {
        memset(stor, 0, rsz);
        return;
    }

    auto *val = static_cast<unsigned char *>(stor);
    auto &pb = decl.ptr_base();
    size_t bsize = pb.alloc_size();
    size_t nelems = rsz / bsize;

    bool base_array = (pb.type() == ast::C_BUILTIN_ARRAY);
    bool base_struct = (pb.type() == ast::C_BUILTIN_RECORD);

    if (!decl.vla() && !decl.unbounded()) {
        if (ninit > int(nelems)) {
            luaL_error(L, "too many initializers");
            return;
        } else if (ninit == 1) {
            /* special case: initialize aggregate with a single value */
            push_init(L, tidx, sidx);
            size_t esz;
            arg_stor_t sv{};
            void *ep = from_lua(L, pb, &sv, -1, esz, RULE_CONV);
            while (nelems) {
                memcpy(val, ep, esz);
                val += bsize;
                --nelems;
            }
            lua_pop(L, 1);
            return;
        }
    }

    for (int rinit = ninit; rinit; --rinit) {
        if ((base_array || base_struct) && lua_istable(L, -1)) {
            int ntidx = lua_gettop(L);
            int nninit;
            int nsidx = get_init_sidx(L, ntidx, nninit);
            from_lua_table(L, pb, val, bsize, ntidx, nsidx, nninit);
        } else {
            size_t esz;
            arg_stor_t sv{};
            push_init(L, tidx, sidx++);
            void *ep = from_lua(L, pb, &sv, -1, esz, RULE_CONV);
            memcpy(val, ep, esz);
        }
        val += bsize;
        lua_pop(L, 1);
    }
    if (ninit < int(nelems)) {
        /* fill possible remaining space with zeroes */
        memset(val, 0, bsize * (nelems - ninit));
    }
}

void get_global(lua_State *L, lib::c_lib const *dl, const char *sname) {
    auto &ds = ast::decl_store::get_main(L);
    auto const *decl = ds.lookup(sname);

    auto tp = ast::c_object_type::INVALID;
    if (decl) {
        tp = decl->obj_type();
    }

    switch (tp) {
        case ast::c_object_type::VARIABLE: {
            void *symp = lib::get_sym(dl, L, sname);
            auto &var = decl->as<ast::c_variable>().type();
            if (var.type() == ast::C_BUILTIN_FUNC) {
                make_cdata_func(
                    L, reinterpret_cast<void (*)()>(symp),
                    var.function(), false, nullptr
                );
            } else {
                to_lua(
                    L, decl->as<ast::c_variable>().type(), symp, RULE_RET
                );
            }
            return;
        }
        case ast::c_object_type::CONSTANT: {
            auto &cd = decl->as<ast::c_constant>();
            to_lua(L, cd.type(), &cd.value(), RULE_RET);
            return;
        }
        default:
            luaL_error(
                L, "missing declaration for symbol '%s'", sname
            );
            return;
    }
}

void set_global(lua_State *L, lib::c_lib const *dl, char const *sname, int idx) {
    auto &ds = ast::decl_store::get_main(L);
    auto const *decl = ds.lookup(sname);
    if (!decl) {
        luaL_error(L, "missing declaration for symbol '%s'", decl->name());
        return;
    }
    if (decl->obj_type() != ast::c_object_type::VARIABLE) {
        luaL_error(L, "symbol '%s' is not mutable", decl->name());
    }
    auto &cv = decl->as<ast::c_variable>().type();
    if (cv.type() == ast::C_BUILTIN_FUNC) {
        luaL_error(L, "symbol '%s' is not mutable", decl->name());
    }

    void *symp = lib::get_sym(dl, L, sname);
    size_t rsz;
    from_lua(L, cv, symp, idx, rsz, RULE_CONV);
}

void make_cdata(lua_State *L, ast::c_type const &decl, int rule, int idx) {
    switch (decl.type()) {
        case ast::C_BUILTIN_FUNC:
            luaL_error(L, "invalid C type");
            break;
        default:
            break;
    }
    arg_stor_t stor{};
    void *cdp = nullptr;
    size_t rsz = 0, narr = 0;
    int iidx = idx, ninits;
    if (rule == RULE_CAST) {
        goto definit;
    }
    if (decl.type() == ast::C_BUILTIN_ARRAY) {
        if (decl.unbounded()) {
            luaL_error(L, "size of C type is unknown");
        } else if (decl.vla()) {
            auto arrs = luaL_checkinteger(L, idx);
            if (arrs < 0) {
                luaL_error(L, "size of C type is unknown");
            }
            ++iidx;
            ninits = lua_gettop(L) - iidx + 1;
            narr = size_t(arrs);
            /* see below */
            rsz = decl.ptr_base().alloc_size() * narr + sizeof(arg_stor_t);
            goto newdata;
        }
        ninits = lua_gettop(L) - iidx + 1;
        narr = decl.array_size();
        /* owned arrays consist of an arg_stor_t part, which is an arg_stor_t
         * because that has the greatest alignment of all scalars and thus is
         * good enough to follow up with any type afterwards, and the array
         * part; the arg_stor_t part contains a pointer to the array part
         * right in the beginning, so we can freely cast between any array
         * and a pointer, even an owned one
         */
        rsz = decl.ptr_base().alloc_size() * narr + sizeof(arg_stor_t);
        goto newdata;
    } else if (decl.type() == ast::C_BUILTIN_RECORD) {
        auto &flds = decl.record().fields();
        if (!flds.empty()) {
            auto &lf = flds.back().type;
            if (lf.unbounded()) {
                auto arrs = luaL_checkinteger(L, idx);
                if (arrs < 0) {
                    luaL_error(L, "size of C type is unknown");
                }
                ++iidx;
                ninits = lua_gettop(L) - iidx + 1;
                rsz = decl.alloc_size() + (
                    size_t(arrs) * lf.ptr_base().alloc_size()
                );
                goto newdata;
            }
        }
        ninits = lua_gettop(L) - iidx + 1;
        rsz = decl.alloc_size();
        goto newdata;
    }
definit:
    ninits = lua_gettop(L) - iidx + 1;
    if (ninits > 1) {
        luaL_error(L, "too many initializers");
    } else if (ninits == 1) {
        cdp = from_lua(L, decl, &stor, idx, rsz, rule);
    } else {
        rsz = decl.alloc_size();
    }
newdata:
    if (decl.callable()) {
        closure_data *cd = nullptr;
        if (cdp && iscdata(L, idx)) {
            /* special handling for closures */
            auto &fcd = tocdata<fdata>(L, idx);
            if (fcd.decl.closure()) {
                cd = fcd.val.cd;
                cdp = nullptr;
            }
        }
        using FP = void (*)();
        make_cdata_func(
            L, cdp ? *reinterpret_cast<FP *>(cdp) : nullptr,
            decl.function(), decl.type() == ast::C_BUILTIN_PTR, cd
        );
        if (!cdp && !cd) {
            tocdata<fdata>(L, -1).val.cd->fref = stor.as<int>();
        }
    } else {
        auto &cd = newcdata(L, decl, rsz);
        void *dptr = nullptr;
        size_t msz = rsz;
        if (!cdp) {
            memset(&cd.val, 0, rsz);
            if (decl.type() == ast::C_BUILTIN_ARRAY) {
                auto *bval = reinterpret_cast<unsigned char *>(&cd.val);
                dptr = bval + sizeof(arg_stor_t);
                *reinterpret_cast<void **>(bval) = dptr;
                msz = rsz - sizeof(arg_stor_t);
            } else {
                dptr = &cd.val;
            }
        } else if (decl.type() == ast::C_BUILTIN_ARRAY) {
            size_t esz = (rsz - sizeof(arg_stor_t)) / narr;
            /* the base of the alloated block */
            auto *bval = reinterpret_cast<unsigned char *>(&cd.val);
            /* the array memory begins after the first arg_stor_t */
            auto *val = bval + sizeof(arg_stor_t);
            dptr = val;
            /* we can treat an array like a pointer, always */
            *reinterpret_cast<void **>(bval) = val;
            /* write initializers into the array part */
            for (size_t i = 0; i < narr; ++i) {
                memcpy(&val[i * esz], cdp, esz);
            }
            msz = rsz - sizeof(arg_stor_t);
        } else {
            dptr = &cd.val;
            memcpy(dptr, cdp, rsz);
        }
        if (ninits && (
            (decl.type() == ast::C_BUILTIN_RECORD) ||
            (decl.type() == ast::C_BUILTIN_ARRAY)
        )) {
            /* initializers for aggregates are done *after* memory alloc
             *
             * we have several cases: either multiple initializers, in which
             * case we treat it as a flat initializer list, or not-a-table,
             * in which case we treat it the same
             *
             * or we have a single table initializer, in which case its
             * contents are used for initialization instead
             */
            if ((ninits > 1) || !lua_istable(L, iidx)) {
                from_lua_table(L, decl, dptr, msz, 0, iidx, ninits);
            } else {
                int nninit;
                int nsidx = get_init_sidx(L, iidx, nninit);
                from_lua_table(L, decl, dptr, msz, iidx, nsidx, nninit);
            }
        }
        /* set a gc finalizer if provided in metatype */
        if (decl.type() == ast::C_BUILTIN_RECORD) {
            int mf;
            int mt = decl.record().metatype(mf);
            if (mf & METATYPE_FLAG_GC) {
                if (metatype_getfield(L, mt, "__gc")) {
                    cd.gc_ref = luaL_ref(L, LUA_REGISTRYINDEX);
                }
            }
        }
    }
}

} /* namespace ffi */
