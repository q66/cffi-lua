#include "platform.hh"
#include "util.hh"
#include "ffi.hh"

namespace ffi {

static void *from_lua(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    std::size_t &dsz, int rule
);

static inline void fail_convert_cd(
    lua_State *L, ast::c_type const &from, ast::c_type const &to
) {
    from.serialize(L);
    to.serialize(L);
    luaL_error(
        L, "cannot convert '%s' to '%s'",
        lua_tostring(L, -2), lua_tostring(L, -1)
    );
}

static inline void fail_convert_tp(
    lua_State *L, char const *from, ast::c_type const &to
) {
    to.serialize(L);
    luaL_error(
        L, "cannot convert '%s' to '%s'",
        from, lua_tostring(L, -1)
    );
}

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
            auto *cd = testcdata(L, index);
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

static inline ffi::scalar_stor_t *&fdata_get_aux(fdata &fd) {
    return *util::pun<ffi::scalar_stor_t **>(fd.args());
}

static inline void fdata_free_aux(lua_State *, fdata &fd) {
    auto &ptr = fdata_get_aux(fd);
    delete[] util::pun<unsigned char *>(ptr);
    ptr = nullptr;
}

static inline void fdata_new_aux(lua_State *, fdata &fd, std::size_t sz) {
    fdata_get_aux(fd) = util::pun<ffi::scalar_stor_t *>(new unsigned char[sz]);
}

static inline ffi_type **fargs_types(ffi::scalar_stor_t *args, std::size_t nargs) {
    /* see memory layout comment below; this accesses the beginning
     * of the ffi_type section within the fdata structure
     */
    return util::pun<ffi_type **>(args + nargs);
}

static inline void **fargs_values(ffi::scalar_stor_t *args, std::size_t nargs) {
    /* this accesses the value pointers that follow the ffi_type pointers */
    return util::pun<void **>(fargs_types(args, nargs) + nargs);
}

void destroy_cdata(lua_State *L, cdata &cd) {
    if (cd.gc_ref >= 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cd.gc_ref);
        lua_pushvalue(L, 1); /* the cdata */
        if (lua_pcall(L, 1, 0, 0)) {
            lua_pop(L, 1);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, cd.gc_ref);
    }
    switch (cd.decl.type()) {
        case ast::C_BUILTIN_PTR:
            if (cd.decl.ptr_base().type() != ast::C_BUILTIN_FUNC) {
                break;
            }
            goto free_aux;
        free_aux:
        case ast::C_BUILTIN_FUNC: {
            if (!cd.decl.function()->variadic()) {
                break;
            }
            fdata_free_aux(L, cd.as<fdata>());
        }
        default:
            break;
    }
    using T = ast::c_type;
    cd.decl.~T();
}

void destroy_closure(lua_State *, closure_data *cd) {
    cd->~closure_data();
    delete[] util::pun<unsigned char *>(cd);
}

static void cb_bind(ffi_cif *, void *ret, void *args[], void *data) {
    auto &fud = *static_cast<cdata *>(data);
    auto &fun = fud.decl.function();
    auto &pars = fun->params();
    auto fargs = pars.size();

    closure_data &cd = *fud.as<fdata>().cd;
    lua_rawgeti(cd.L, LUA_REGISTRYINDEX, cd.fref);
    for (std::size_t i = 0; i < fargs; ++i) {
        to_lua(cd.L, pars[i].type(), args[i], RULE_PASS, false);
    }

    if (fun->result().type() != ast::C_BUILTIN_VOID) {
        lua_call(cd.L, int(fargs), 1);
        ffi::scalar_stor_t stor{};
        std::size_t rsz;
        void *rp = from_lua(
            cd.L, fun->result(), &stor, -1, rsz, RULE_RET
        );
        std::memcpy(ret, rp, rsz);
        lua_pop(cd.L, 1);
    } else {
        lua_call(cd.L, int(fargs), 0);
    }
}

#if defined(FFI_WINDOWS_ABI) && (FFI_ARCH == FFI_ARCH_X86)
static inline ffi_abi to_libffi_abi(int conv) {
    switch (conv) {
        case ast::C_FUNC_DEFAULT:
            return FFI_DEFAULT_ABI;
        case ast::C_FUNC_CDECL:
            return FFI_MS_CDECL;
        case ast::C_FUNC_FASTCALL:
            return FFI_FASTCALL;
        case ast::C_FUNC_STDCALL:
            return FFI_STDCALL;
        case ast::C_FUNC_THISCALL:
            return FFI_THISCALL;
        default:
            break;
    }
    assert(false);
    return FFI_DEFAULT_ABI;
}
#else
static inline ffi_abi to_libffi_abi(int) {
    return FFI_DEFAULT_ABI;
}
#endif

/* this initializes a non-vararg cif with the given number of arguments
 * for variadics, this is initialized once for zero args, and then handled
 * dynamically before every call
 */
static bool prepare_cif(
    util::rc_obj<ast::c_function> const &func, ffi_cif &cif,
    ffi_type **targs, std::size_t nargs
) {
    for (std::size_t i = 0; i < nargs; ++i) {
        targs[i] = func->params()[i].libffi_type();
    }
    using U = unsigned int;
    return (ffi_prep_cif(
        &cif, to_libffi_abi(func->callconv()), U(nargs),
        func->result().libffi_type(), targs
    ) == FFI_OK);
}

static void make_cdata_func(
    lua_State *L, void (*funp)(), util::rc_obj<ast::c_function> func, bool fptr,
    closure_data *cd
) {
    auto nargs = func->params().size();

    /* MEMORY LAYOUT:
     *
     * regular func:
     *
     * struct cdata {
     *     <cdata header>
     *     struct fdata {
     *         <fdata header>
     *         ffi::scalar_stor_t val1; // lua arg1
     *         ffi::scalar_stor_t val2; // lua arg2
     *         ffi::scalar_stor_t valN; // lua argN
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
    ast::c_type funct{func, 0, funp == nullptr};
    auto &fud = newcdata(
        L, fptr ? ast::c_type{
            util::make_rc<ast::c_type>(util::move(funct)),
            0, ast::C_BUILTIN_PTR
        } : util::move(funct),
        sizeof(fdata) + (func->variadic() ? sizeof(void *) : (
            sizeof(ffi::scalar_stor_t) * nargs + sizeof(void *) * nargs * 2
        ))
    );
    fud.as<fdata>().sym = funp;

    if (func->variadic()) {
        fdata_get_aux(fud.as<fdata>()) = nullptr;
        if (!funp) {
            luaL_error(L, "variadic callbacks are not supported");
        }
        nargs = 0;
    }

    if (!prepare_cif(
        func, fud.as<fdata>().cif,
        fargs_types(fud.as<fdata>().args(), nargs), nargs
    )) {
        luaL_error(L, "unexpected failure setting up '%s'", func->name());
    }

    if (!funp) {
        /* no funcptr means we're setting up a callback */
        if (cd) {
            /* copying existing callback reference */
            fud.as<fdata>().cd = cd;
            return;
        }
        cd = util::pun<closure_data *>(new unsigned char[
            sizeof(closure_data) + nargs * sizeof(ffi_type *)
        ]);
        new (cd) closure_data{};
        /* allocate a closure in it */
        void *symp;
        cd->closure = static_cast<ffi_closure *>(
            ffi_closure_alloc(sizeof(ffi_closure), &symp)
        );
        std::memcpy(&fud.as<fdata>().sym, &symp, sizeof(void *));
        if (!cd->closure) {
            destroy_closure(L, cd);
            func->serialize(L);
            luaL_error(
                L, "failed allocating callback for '%s'",
                lua_tostring(L, -1)
            );
        }
        /* arg pointers follow closure data struct, as allocated above */
        auto **targs = util::pun<ffi_type **>(cd + 1);
        if (!prepare_cif(fud.decl.function(), cd->cif, targs, nargs)) {
            destroy_closure(L, cd);
            luaL_error(L, "unexpected failure setting up '%s'", func->name());
        }
        if (ffi_prep_closure_loc(
            cd->closure, &fud.as<fdata>().cif, cb_bind, &fud, symp
        ) != FFI_OK) {
            destroy_closure(L, cd);
            func->serialize(L);
            luaL_error(
                L, "failed initializing closure for '%s'",
                lua_tostring(L, -1)
            );
        }
        cd->L = L;
        fud.as<fdata>().cd = cd;
    }
}

static bool prepare_cif_var(
    lua_State *L, cdata &fud, std::size_t nargs, std::size_t fargs
) {
    auto &func = fud.decl.function();

    auto &auxptr = fdata_get_aux(fud.as<fdata>());
    if (auxptr && (nargs > std::size_t(fud.aux))) {
        fdata_free_aux(L, fud.as<fdata>());
    }
    if (!auxptr) {
        fdata_new_aux(
            L, fud.as<fdata>(),
            nargs * sizeof(ffi::scalar_stor_t) + 2 * nargs * sizeof(void *)
        );
        fud.aux = int(nargs);
    }

    ffi_type **targs = fargs_types(auxptr, nargs);
    for (std::size_t i = 0; i < fargs; ++i) {
        targs[i] = func->params()[i].libffi_type();
    }
    for (std::size_t i = fargs; i < nargs; ++i) {
        targs[i] = lua_to_vararg(L, int(i + 2));
    }

    using U = unsigned int;
    return (ffi_prep_cif_var(
        &fud.as<fdata>().cif, to_libffi_abi(func->callconv()), U(fargs), U(nargs),
        func->result().libffi_type(), targs
    ) == FFI_OK);
}

int call_cif(cdata &fud, lua_State *L, std::size_t largs) {
    auto &func = fud.decl.function();
    auto &pdecls = func->params();

    auto nargs = pdecls.size();
    auto targs = nargs;

    ffi::scalar_stor_t *pvals = fud.as<fdata>().args();
    void *rval = fdata_retval(fud.as<fdata>());

    if (func->variadic()) {
        targs = util::max(largs, nargs);
        if (!prepare_cif_var(L, fud, targs, nargs)) {
            luaL_error(L, "unexpected failure setting up '%s'", func->name());
        }
        pvals = fdata_get_aux(fud.as<fdata>());
    }

    void **vals = fargs_values(pvals, targs);
    /* fixed args */
    for (int i = 0; i < int(nargs); ++i) {
        std::size_t rsz;
        vals[i] = from_lua(
            L, pdecls[i].type(), &pvals[i], i + 2, rsz, RULE_PASS
        );
    }
    /* variable args */
    for (int i = int(nargs); i < int(targs); ++i) {
        std::size_t rsz;
        auto tp = ast::from_lua_type(L, i + 2);
        if (tp.type() == ast::C_BUILTIN_RECORD) {
            /* special case for vararg passing of records: by ptr */
            auto &cd = tocdata(L, i + 2);
            std::memcpy(&pvals[i], cd.as_ptr(), sizeof(void *));
            continue;
        }
        vals[i] = from_lua(L, util::move(tp), &pvals[i], i + 2, rsz, RULE_PASS);
    }

    ffi_call(&fud.as<fdata>().cif, fud.as<fdata>().sym, rval, vals);
    return to_lua(L, func->result(), rval, RULE_RET, true);
}

template<typename T>
static inline int push_int(
    lua_State *L, ast::c_type const &tp, void const *value, bool rv, bool lossy
) {
#if LUA_VERSION_NUM < 503
    /* generally floats, so we're assuming IEEE754 binary floats */
    static_assert(
        util::limit_radix<lua_Number>() == 2, "unsupported lua_Number type"
    );
    using LT = lua_Number;
#else
    /* on lua 5.3+, we can use integers builtin in the language instead */
    using LT = lua_Integer;
#endif
    T actual_val;
    if (rv && (sizeof(T) < sizeof(ffi_sarg))) {
        using U = ffi_sarg *;
        actual_val = T(*U(value));
    } else {
        using U = T *;
        actual_val = *U(value);
    }
    if ((util::limit_digits<T>() <= util::limit_digits<LT>()) || lossy) {
        lua_pushinteger(L, lua_Integer(actual_val));
        return 1;
    }
    /* doesn't fit into the range, so make scalar cdata */
    auto &cd = newcdata(L, tp, sizeof(T));
    std::memcpy(cd.as_ptr(), &actual_val, sizeof(T));
    return 1;
}

template<typename T>
static inline int push_flt(
    lua_State *L, ast::c_type const &tp, void const *value, bool lossy
) {
    /* probably not the best check */
    if ((util::limit_max<T>() <= util::limit_max<lua_Number>()) || lossy) {
        using U = T *;
        lua_pushnumber(L, lua_Number(*U(value)));
        return 1;
    }
    auto &cd = newcdata(L, tp, sizeof(T));
    std::memcpy(cd.as_ptr(), value, sizeof(T));
    return 1;
}

int to_lua(
    lua_State *L, ast::c_type const &tp, void const *value,
    int rule, bool ffi_ret, bool lossy
) {
    if (tp.is_ref()) {
        /* dereference regardless */
        auto *dval = *static_cast<void * const *>(value);
        if (tp.type() == ast::C_BUILTIN_FUNC) {
            make_cdata_func(
                L, util::pun<void (*)()>(dval), tp.function(),
                rule != RULE_CONV, nullptr
            );
            return 1;
        }
        if (rule == RULE_CONV) {
            /* dereference, continue as normal */
            value = dval;
            ffi_ret = false;
        } else {
            /* reference cdata */
            newcdata(L, tp, sizeof(void *)).as<void *>() = dval;
            return 1;
        }
    }

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
            return push_int<char>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_SCHAR:
            return push_int<signed char>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_UCHAR:
            return push_int<unsigned char>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_SHORT:
            return push_int<short>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_USHORT:
            return push_int<unsigned short>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_INT:
            return push_int<int>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_UINT:
            return push_int<unsigned int>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_LONG:
            return push_int<long>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_ULONG:
            return push_int<unsigned long>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_LLONG:
            return push_int<long long>(L, tp, value, ffi_ret, lossy);
        case ast::C_BUILTIN_ULLONG:
            return push_int<unsigned long long>(L, tp, value, ffi_ret, lossy);

        case ast::C_BUILTIN_PTR:
            if (tp.ptr_base().type() == ast::C_BUILTIN_FUNC) {
                return to_lua(L, tp.ptr_base(), value, rule, false, lossy);
            }
            /* pointers should be handled like large cdata, as they need
             * to be represented as userdata objects on lua side either way
             */
            newcdata(L, tp, sizeof(void *)).as<void *>() =
                *static_cast<void * const *>(value);
            return 1;

        case ast::C_BUILTIN_VA_LIST:
            newcdata(L, tp, sizeof(void *)).as<void *>() =
                *static_cast<void * const *>(value);
            return 1;

        case ast::C_BUILTIN_FUNC: {
            make_cdata_func(
                L, util::pun<void (*)()>(*static_cast<void * const *>(value)),
                tp.function(), true, nullptr
            );
            return 1;
        }

        case ast::C_BUILTIN_ENUM:
            /* TODO: large enums */
            return push_int<int>(L, tp, value, ffi_ret, lossy);

        case ast::C_BUILTIN_ARRAY: {
            if (rule == RULE_PASS) {
                /* pass rule: only when passing to array args in callbacks
                 * in this case we just drop the array bit and use a pointer
                 */
                newcdata(
                    L, tp.as_type(ast::C_BUILTIN_PTR), sizeof(void *)
                ).as<void *>() = *static_cast<void * const *>(value);
                return 1;
            }
            /* this case may be encountered twice, when retrieving array
             * members of cdata, or when retrieving global array cdata; any
             * other cases are not possible (e.g. you can't return an array)
             *
             * we need to create a C++ style reference in possible cases
             */
            auto &cd = newcdata(L, tp.as_ref(), sizeof(void *) * 2);
            cd.as<void const *[2]>()[1] = value;
            cd.as<void const *[2]>()[0] = &cd.as<void const *[2]>()[1];
            return 1;
        }

        case ast::C_BUILTIN_RECORD: {
            if (rule == RULE_CONV) {
                newcdata(L, tp.as_ref(), sizeof(void *)).as<void const *>() = value;
                return 1;
            }
            auto sz = tp.alloc_size();
            auto &cd = newcdata(L, tp, sz);
            std::memcpy(cd.as_ptr(), value, sz);
            return 1;
        }

        case ast::C_BUILTIN_INVALID:
            break;
    }

    luaL_error(L, "unexpected error: unhandled type %d", tp.type());
    return 0;
}

template<typename T>
static inline void write_int(
    lua_State *L, int index, void *stor, std::size_t &s
) {
    if (lua_isinteger(L, index)) {
        *static_cast<T *>(stor) = T(lua_tointeger(L, index));
    } else if (lua_isboolean(L, index)) {
        *static_cast<T *>(stor) = T(lua_toboolean(L, index));
    } else {
        *static_cast<T *>(stor) = T(lua_tonumber(L, index));
    }
    s = sizeof(T);
}

template<typename T>
static inline void write_flt(
    lua_State *L, int index, void *stor, std::size_t &s
) {
    lua_Number v = lua_isboolean(L, index) ?
        lua_toboolean(L, index) : lua_tonumber(L, index);
    *static_cast<T *>(stor) = T(v);
    s = sizeof(T);
}

static void from_lua_num(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    std::size_t &dsz, int rule
) {
    if (tp.is_ref() && (rule == RULE_CAST)) {
        dsz = sizeof(void *);
        *static_cast<void **>(stor) = util::pun<void *>(
            std::size_t(lua_tointeger(L, index))
        );
        return;
    }

    switch (ast::c_builtin(tp.type())) {
        case ast::C_BUILTIN_FLOAT:
            write_flt<float>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_DOUBLE:
            write_flt<double>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_LDOUBLE:
            write_flt<long double>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_BOOL:
            write_int<bool>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_CHAR:
            write_int<char>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_SCHAR:
            write_int<signed char>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_UCHAR:
            write_int<unsigned char>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_SHORT:
            write_int<short>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_USHORT:
            write_int<unsigned short>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_INT:
            write_int<int>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_UINT:
            write_int<unsigned int>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_LONG:
            write_int<long>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_ULONG:
            write_int<unsigned long>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_LLONG:
            write_int<long long>(L, index, stor, dsz);
            return;
        case ast::C_BUILTIN_ULLONG:
            write_int<unsigned long long>(
                L, index, stor, dsz
            );
            return;

        case ast::C_BUILTIN_ENUM:
            /* TODO: large enums */
            write_int<int>(L, index, stor, dsz);
            return;

        case ast::C_BUILTIN_PTR:
            if (rule == RULE_CAST) {
                dsz = sizeof(void *);
                *static_cast<void **>(stor) = util::pun<void *>(
                    std::size_t(lua_tointeger(L, index))
                );
                return;
            }
            goto converr;

        converr:
        case ast::C_BUILTIN_VOID:
        case ast::C_BUILTIN_RECORD:
        case ast::C_BUILTIN_ARRAY:
        case ast::C_BUILTIN_VA_LIST:
            fail_convert_tp(L, lua_typename(L, lua_type(L, index)), tp);
            break;

        case ast::C_BUILTIN_FUNC:
        case ast::C_BUILTIN_INVALID:
            /* this should not happen */
            tp.serialize(L);
            luaL_error(L, "bad argument type '%s'", lua_tostring(L, -1));
            break;
    }
    assert(false);
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
    auto &fpb = from.is_ref() ? from : from.ptr_base();
    auto &tpb = to.is_ref() ? to : to.ptr_base();
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
    return fpb.is_same(tpb, true, true);
}

/* converting from cdata: pointer */
static void from_lua_cdata_ptr(
    lua_State *L, ast::c_type const &cd, ast::c_type const &tp, int rule
) {
    /* converting to reference */
    if (tp.is_ref()) {
        goto isptr;
    }
    switch (tp.type()) {
        /* converting to pointer */
        case ast::C_BUILTIN_PTR:
            break;
        /* converting to array is okay when passing,
         * e.g. void foo(int[2]) being given int *
         */
        case ast::C_BUILTIN_ARRAY:
            if (rule == RULE_PASS) {
                break;
            }
            fail_convert_cd(L, cd, tp);
            break;
        /* converting to anything else */
        default:
            if ((rule == RULE_CAST) && tp.integer()) {
                /* casting to integer types is fine, it's the user's
                 * responsibility to ensure it's safe by using correct types
                 */
                return;
            }
            fail_convert_cd(L, cd, tp);
            break;
    }

isptr:
    if (rule == RULE_CAST) {
        /* casting: disregard any typing rules */
        return;
    }
    /* initializing a function pointer/reference */
    if (tp.ptr_ref_base().type() == ast::C_BUILTIN_FUNC) {
        if (cd.type() == ast::C_BUILTIN_FUNC) {
            /* plain function: check convertible, init from addr */
            if (!func_convertible(
                *cd.function(), *tp.ptr_base().function()
            )) {
                fail_convert_cd(L, cd, tp);
            }
            return;
        } else if ((cd.type() != ast::C_BUILTIN_PTR) && !cd.is_ref()) {
            /* otherwise given value must be a pointer/ref */
            fail_convert_cd(L, cd, tp);
        }
        /* it must be a pointer/ref to function */
        if (cd.ptr_ref_base().type() != ast::C_BUILTIN_FUNC) {
            fail_convert_cd(L, cd, tp);
        }
        /* and it must satisfy convertible check */
        if (!func_convertible(
            *cd.ptr_ref_base().function(), *tp.ptr_ref_base().function()
        )) {
            fail_convert_cd(L, cd, tp);
        }
        /* then init from address */
        return;
    }
    if (!ptr_convertible(cd, tp)) {
        fail_convert_cd(L, cd, tp);
    }
}

template<typename T>
static void from_lua_cnumber(
    lua_State *L, ast::c_type const &cd, ast::c_type const &tp,
    void *sval, void *stor, std::size_t &dsz, int rule
) {
#define CONV_CASE(name, U) \
    case ast::C_BUILTIN_##name: { \
        dsz = sizeof(U); \
        T val; \
        std::memcpy(&val, sval, sizeof(T)); \
        *static_cast<U *>(stor) = static_cast<U>(val); \
        return; \
    }

    if (tp.is_ref()) {
        goto ptr_ref;
    }

    switch (tp.type()) {
        case ast::C_BUILTIN_PTR:
            goto ptr_ref;
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
            fail_convert_cd(L, cd, tp);
            return;
    }

#undef CONV_CASE

ptr_ref:
    /* only for cast we can initialize pointers with integer addrs */
    if (rule != RULE_CAST) {
        fail_convert_cd(L, cd, tp);
        return;
    }
    /* must not be floating point */
    if (!util::is_int<T>::value) {
        fail_convert_cd(L, cd, tp);
        return;
    }
    dsz = sizeof(void *);
    *static_cast<void **>(stor) = util::pun<void *>(
        std::size_t(*static_cast<T *>(sval))
    );
}

static void *from_lua_cdata(
    lua_State *L, ast::c_type const &cd, ast::c_type const &tp, void *sval,
    void *stor, std::size_t &dsz, int rule
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
    if ((rule == RULE_PASS) && tp.is_ref()) {
        if (cd.is_ref()) {
            return from_lua_cdata(
                L, cd.unref(), tp, *static_cast<void **>(sval),
                stor, dsz, rule
            );
        }
        if (!cv_convertible(cd.cv(), tp.cv())) {
            fail_convert_cd(L, cd, tp);
        }
        if (!cd.is_same(tp, true, true)) {
            fail_convert_cd(L, cd, tp);
        }
        dsz = sizeof(void *);
        return &(*static_cast<void **>(stor) = sval);
    }
    if (cd.is_ref()) {
        /* always dereference */
        return from_lua_cdata(
            L, cd.unref(), tp, *static_cast<void **>(sval),
            stor, dsz, rule
        );
    }
    switch (cd.type()) {
        case ast::C_BUILTIN_PTR:
            from_lua_cdata_ptr(L, cd, tp, rule);
            dsz = sizeof(void *);
            return sval;
        case ast::C_BUILTIN_FUNC:
            if ((tp.type() != ast::C_BUILTIN_PTR) && !tp.is_ref()) {
                /* converting from func: must be to some kind of pointer */
                fail_convert_cd(L, cd, tp);
            }
            if (rule == RULE_CAST) {
                /* casting: ignore rules, convert to any pointer */
                dsz = sizeof(void *);
                return sval;
            }
            /* not casting: some rules must be followed */
            if (tp.ptr_ref_base().type() != ast::C_BUILTIN_FUNC) {
                fail_convert_cd(L, cd, tp);
            }
            if (!func_convertible(*cd.function(), *tp.ptr_ref_base().function())) {
                fail_convert_cd(L, cd, tp);
            }
            dsz = sizeof(void *);
            return sval;
        case ast::C_BUILTIN_RECORD: {
            /* we can pass structs by value in non-cast context,
             * as well as pointers and references by address
             */
            bool do_copy = ((tp.type() != ast::C_BUILTIN_PTR) && !tp.is_ref());
            if (do_copy && (rule == RULE_CAST)) {
                break;
            }
            if (rule != RULE_CAST) {
                if (do_copy) {
                    if (!cd.is_same(tp, true)) {
                        break;
                    }
                } else {
                    if (!cv_convertible(cd.cv(), tp.ptr_ref_base().cv())) {
                        break;
                    }
                }
            }
            if (do_copy) {
                dsz = cd.alloc_size();
                return sval;
            } else {
            }
            dsz = sizeof(void *);
            return &(*static_cast<void **>(stor) = sval);
        }
        default:
            if (cd.is_same(tp, true)) {
                dsz = cd.alloc_size();
                return sval;
            }
            break;
    }

#define CONV_CASE(name, T) \
    case ast::C_BUILTIN_##name: \
        from_lua_cnumber<T>(L, cd, tp, sval, stor, dsz, rule); \
        return stor;

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

    fail_convert_cd(L, cd, tp);
    return nullptr;
}

/* this returns a pointer to a C value counterpart of the Lua value
 * on the stack (as given by `index`) while checking types (`rule`)
 *
 * necessary conversions are done according to `tp`; `stor` is used to
 * write scalar values (therefore its alignment and size must be enough
 * to fit the converted value - the ffi::scalar_stor_t type can store any
 * scalar so you can use that) while non-scalar values may have their address
 * returned directly
 */
static void *from_lua(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    std::size_t &dsz, int rule
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
            if (tp.is_ref() || (tp.type() == ast::C_BUILTIN_PTR)) {
                dsz = sizeof(void *);
                return &(*static_cast<void **>(stor) = nullptr);
            }
            fail_convert_tp(L, "nil", tp);
            break;
        case LUA_TNUMBER:
        case LUA_TBOOLEAN:
            from_lua_num(L, tp, stor, index, dsz, rule);
            return stor;
        case LUA_TSTRING:
            if ((rule == RULE_CAST) || (
                (tp.type() == ast::C_BUILTIN_PTR) &&
                (
                    (tp.ptr_base().type() == ast::C_BUILTIN_CHAR) ||
                    (tp.ptr_base().type() == ast::C_BUILTIN_VOID)
                ) &&
                (tp.ptr_base().cv() & ast::C_CV_CONST)
            )) {
                dsz = sizeof(char const *);
                return &(
                    *static_cast<char const **>(stor) = lua_tostring(L, index)
                );
            }
            fail_convert_tp(L, "string", tp);
            break;
        case LUA_TUSERDATA: {
            if (iscdata(L, index)) {
                auto &cd = *lua::touserdata<cdata>(L, index);
                return from_lua_cdata(
                    L, cd.decl, tp, cd.as_ptr(), stor, dsz, rule
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
            } else if (tp.is_ref() && (rule == RULE_CAST)) {
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
                fail_convert_tp(L, "ctype", tp);
            } else {
                fail_convert_tp(L, "userdata", tp);
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
                fail_convert_tp(L, "lightuserdata", tp);
            }
            break;
        case LUA_TTABLE:
            /* we can't handle table initializers here because the memory
             * for the new cdata doesn't exist yet by this point, and it's
             * this function that tells the caller how much memory we'll
             * actually need, and return a pointer to copy from...
             *
             * there are only three special cases where initialization from
             * table is supported, and that is ffi.new, assignment to struct
             * or array members of structs or arrays, and global variable
             * assignment, and those are all handled much earlier so this
             * is never reached
             *
             * so here, we just error, as it definitely means a bad case
             */
            fail_convert_tp(L, "table", tp);
            break;
        case LUA_TFUNCTION:
            if (!tp.callable()) {
                fail_convert_tp(L, "function", tp);
            }
            lua_pushvalue(L, index);
            *static_cast<int *>(stor) = luaL_ref(L, LUA_REGISTRYINDEX);
            /* we don't have a value to store */
            return nullptr;
        default:
            fail_convert_tp(L, lua_typename(L, lua_type(L, index)), tp);
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

static void from_lua_table(
    lua_State *L, ast::c_type const &decl, void *stor, std::size_t rsz,
    int tidx, int sidx, int ninit
);

static void from_lua_table(
    lua_State *L, ast::c_type const &decl, void *stor, std::size_t rsz,
    int tidx
);

static void from_lua_str(
    lua_State *L, ast::c_type const &decl,
    void *stor, std::size_t dsz, int idx, std::size_t nelems = 1,
    std::size_t bsize = 0
) {
    std::size_t vsz;
    ffi::scalar_stor_t sv{};
    void const *vp;
    auto *val = static_cast<unsigned char *>(stor);
    /* not a string: let whatever default behavior happen */
    if (lua_type(L, idx) != LUA_TSTRING) {
        goto fallback;
    }
    /* not an array: we can just initialize normally too */
    if (decl.type() != ast::C_BUILTIN_ARRAY) {
        goto fallback;
    }
    /* string value, not byte array: let it fail */
    if (!decl.ptr_base().byte()) {
        goto fallback;
    }
    /* char-like array, string value */
    vp = lua_tolstring(L, idx, &vsz);
    /* add 1 because of null terminator, but use at most the given space */
    vsz = util::min(vsz + 1, dsz);
    goto cloop;
fallback:
    vp = from_lua(L, decl, &sv, idx, vsz, RULE_CONV);
cloop:
    while (nelems) {
        std::memcpy(val, vp, vsz);
        val += bsize;
        --nelems;
    }
}

static void from_lua_table_record(
    lua_State *L, ast::c_type const &decl, void *stor, std::size_t rsz,
    int tidx, int sidx, int ninit
) {
    auto &sb = decl.record();
    bool uni = sb.is_union();
    auto *val = static_cast<unsigned char *>(stor);
    bool filled = false;
    bool empty = true;
    sb.iter_fields([
        L, rsz, val, &decl, &sidx, &filled, &empty, &ninit, tidx, uni
    ](
        char const *fname, ast::c_type const &fld, std::size_t off
    ) {
        empty = false;
        if (tidx && (ninit < 0)) {
            lua_getfield(L, tidx, fname);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                /* only end at flex members; with unions we want to check
                 * if we can init any other field, with structs we want
                 * to continue initializing other fields
                 */
                return fld.flex();
            }
        } else if (ninit) {
            push_init(L, tidx, sidx++);
            --ninit;
        } else {
            /* no more items to initialize */
            return true;
        }
        /* flex array members */
        if (!uni && fld.flex()) {
            /* the size of the struct minus the flex member plus padding */
            std::size_t ssz = decl.alloc_size();
            /* initialize the last part as in array */
            std::size_t asz = rsz - ssz;
            if (lua_istable(L, -1)) {
                from_lua_table(L, fld, &val[ssz], asz, lua_gettop(L));
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
            from_lua_table(L, fld, &val[off], fld.alloc_size(), lua_gettop(L));
        } else {
            from_lua_str(L, fld, &val[off], fld.alloc_size(), -1);
        }
        filled = true;
        lua_pop(L, 1);
        /* with unions we're only ever initializing one field */
        return uni;
    });
    if (empty) {
        return;
    }
    if (uni && !filled) {
        std::memset(stor, 0, rsz);
    }
}

/* this can't be done in from_lua, because when from_lua is called, the
 * memory is not allocated yet... so do it here, as a special case
 */
static void from_lua_table(
    lua_State *L, ast::c_type const &decl, void *stor, std::size_t rsz,
    int tidx, int sidx, int ninit
) {
    if (decl.type() == ast::C_BUILTIN_RECORD) {
        from_lua_table_record(L, decl, stor, rsz, tidx, sidx, ninit);
        return;
    }

    if (ninit <= 0) {
        std::memset(stor, 0, rsz);
        return;
    }

    auto *val = static_cast<unsigned char *>(stor);
    auto &pb = decl.ptr_base();
    auto bsize = pb.alloc_size();
    auto nelems = rsz / bsize;

    bool base_array = (pb.type() == ast::C_BUILTIN_ARRAY);
    bool base_struct = (pb.type() == ast::C_BUILTIN_RECORD);

    if (!decl.flex()) {
        if (ninit > int(nelems)) {
            luaL_error(L, "too many initializers");
            return;
        } else if (ninit == 1) {
            /* special case: initialize aggregate with a single value */
            push_init(L, tidx, sidx);
            from_lua_str(L, pb, val, bsize, -1, nelems, bsize);
            lua_pop(L, 1);
            return;
        }
    }

    for (int rinit = ninit; rinit; --rinit) {
        if ((base_array || base_struct) && lua_istable(L, -1)) {
            from_lua_table(L, pb, val, bsize, lua_gettop(L));
        } else {
            push_init(L, tidx, sidx++);
            from_lua_str(L, pb, val, bsize, -1);
        }
        val += bsize;
        lua_pop(L, 1);
    }
    if (ninit < int(nelems)) {
        /* fill possible remaining space with zeroes */
        std::memset(val, 0, bsize * (nelems - ninit));
    }
}

static void from_lua_table(
    lua_State *L, ast::c_type const &decl, void *stor, std::size_t rsz,
    int tidx
) {
    int ninit;
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
    } else {
        ninit = -1;
        sidx = -1;
    }
    from_lua_table(L, decl, stor, rsz, tidx, sidx, ninit);
}

/* a unified entrypoint for initializing complex aggregates
 *
 * if false is returned, we're not initializing a complex aggregate,
 * so appropriate steps can be taken according to where this is used
 */
static bool from_lua_aggreg(
    lua_State *L, ast::c_type const &decl, void *stor, std::size_t msz,
    int ninit, int idx
) {
    /* bail out early */
    if (decl.is_ref() || !ninit) {
        return false;
    }
    switch (decl.type()) {
        case ast::C_BUILTIN_RECORD:
            /* record types are simpler */
            if (ninit > 1) {
                /* multiple initializers are clear, init members */
                from_lua_table(L, decl, stor, msz, 0, idx, ninit);
            } else if (!lua_istable(L, idx)) {
                /* single non-table initializer case */
                if (iscdata(L, idx)) {
                    /* got cdata as initializer */
                    auto &cd = *lua::touserdata<cdata>(L, idx);
                    if (cd.decl.is_same(decl, true, true)) {
                        /* it's a compatible type: do a copy */
                        std::size_t vsz;
                        ffi::scalar_stor_t sv{};
                        auto *vp = from_lua(L, decl, &sv, idx, vsz, RULE_CONV);
                        std::memcpy(stor, vp, msz);
                        return true;
                    }
                }
                /* otherwise, just init members using the single value */
                from_lua_table(L, decl, stor, msz, 0, idx, ninit);
            } else {
                /* table initializer: init members */
                from_lua_table(L, decl, stor, msz, idx);
            }
            return true;
        case ast::C_BUILTIN_ARRAY:
            break;
        default:
            return false;
    }
    /* arrays are more complicated, let's start with the clear
     * case which is multiple initializers, no choices there
     */
    if (ninit > 1) {
        from_lua_table(L, decl, stor, msz, 0, idx, ninit);
        return true;
    }
    /* single string initializer */
    auto carr = decl.ptr_base().byte();
    if (carr && (lua_type(L, idx) == LUA_TSTRING)) {
        from_lua_str(L, decl, stor, msz, idx);
        return true;
    }
    /* single table initializer */
    if (lua_istable(L, idx)) {
        from_lua_table(L, decl, stor, msz, idx);
        return true;
    }
    /* single initializer that is a compatible array
     *
     * VLAs are not allowed for this kind of initialization
     * the other array must have the same size
     */
    if (!decl.vla() && iscdata(L, idx)) {
        auto &cd = *lua::touserdata<cdata>(L, idx);
        if (cd.decl.is_same(decl, true, true) || (
            carr && cd.decl.ptr_base().byte() &&
            (cd.decl.array_size() == decl.array_size())
        )) {
            /* exact copy by value */
            std::memcpy(stor, *static_cast<void **>(cd.as_deref_ptr()), msz);
            return true;
        }
    }
    /* a single non-table initializer that is not a compatible array */
    from_lua_table(L, decl, stor, msz, 0, idx, ninit);
    return true;
}

void from_lua(lua_State *L, ast::c_type const &decl, void *stor, int idx) {
    if (decl.cv() & ast::C_CV_CONST) {
        luaL_error(L, "attempt to write to constant location");
    }
    /* attempt aggregate initialization */
    if (!from_lua_aggreg(L, decl, stor, decl.alloc_size(), 1, idx)) {
        /* fall back to regular initialization */
        ffi::scalar_stor_t sv{};
        std::size_t rsz;
        auto *vp = from_lua(L, decl, &sv, idx, rsz, RULE_CONV);
        if (decl.callable() && !vp) {
            make_cdata_func(
                L, nullptr, decl.function(),
                decl.type() == ast::C_BUILTIN_PTR, nullptr
            );
            auto &fd = tocdata(L, -1);
            fd.as<fdata>().cd->fref = util::pun<int>(sv);
            *static_cast<void (**)()>(stor) = fd.as<fdata>().sym;
            lua_pop(L, 1);
        } else {
            std::memcpy(stor, vp, rsz);
        }
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
            auto &var = decl->as<ast::c_variable>();
            void *symp = lib::get_sym(dl, L, var.sym());
            if (var.type().type() == ast::C_BUILTIN_FUNC) {
                make_cdata_func(
                    L, util::pun<void (*)()>(symp), var.type().function(),
                    false, nullptr
                );
            } else {
                to_lua(L, var.type(), symp, RULE_RET, false);
            }
            return;
        }
        case ast::c_object_type::CONSTANT: {
            auto &cd = decl->as<ast::c_constant>();
            to_lua(L, cd.type(), &cd.value(), RULE_RET, false);
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
        luaL_error(L, "missing declaration for symbol '%s'", sname);
        return;
    }
    if (decl->obj_type() != ast::c_object_type::VARIABLE) {
        luaL_error(L, "symbol '%s' is not mutable", decl->name());
    }
    auto &cv = decl->as<ast::c_variable>();
    if (cv.type().type() == ast::C_BUILTIN_FUNC) {
        luaL_error(L, "symbol '%s' is not mutable", decl->name());
    }
    from_lua(L, cv.type(), lib::get_sym(dl, L, cv.sym()), idx);
}

void make_cdata(lua_State *L, ast::c_type const &decl, int rule, int idx) {
    switch (decl.type()) {
        case ast::C_BUILTIN_FUNC:
            luaL_error(L, "invalid C type");
            break;
        default:
            break;
    }
    ffi::scalar_stor_t stor{};
    void *cdp = nullptr;
    std::size_t rsz = 0, narr = 0;
    int iidx = idx, ninits;
    if (rule == RULE_CAST) {
        goto definit;
    }
    if (decl.type() == ast::C_BUILTIN_ARRAY) {
        if (decl.vla()) {
            auto arrs = luaL_checkinteger(L, idx);
            if (arrs < 0) {
                luaL_error(L, "size of C type is unknown");
            }
            ++iidx;
            ninits = lua_gettop(L) - iidx + 1;
            narr = std::size_t(arrs);
            rsz = decl.ptr_base().alloc_size() * narr;
            /* see below */
            rsz += sizeof(ffi::scalar_stor_t);
            goto newdata;
        } else if (decl.flex()) {
            luaL_error(L, "size of C type is unknown");
        }
        ninits = lua_gettop(L) - iidx + 1;
        narr = decl.array_size();
        rsz = decl.ptr_base().alloc_size() * narr;
        /* owned arrays consist of an ffi::scalar_stor_t part, which is an
         * ffi::scalar_stor_t because that has the greatest alignment of all
         * scalars and thus is good enough to follow up with any type after
         * that, and the array part; the ffi::scalar_stor_t part contains a
         * pointer to the array part right in the beginning, so we can freely
         * cast between any array and a pointer, even an owned one
         */
        rsz += sizeof(ffi::scalar_stor_t);
        goto newdata;
    } else if (decl.type() == ast::C_BUILTIN_RECORD) {
        ast::c_type const *lf = nullptr;
        if (decl.record().flexible(&lf)) {
            auto arrs = luaL_checkinteger(L, idx);
            if (arrs < 0) {
                luaL_error(L, "size of C type is unknown");
            }
            ++iidx;
            ninits = lua_gettop(L) - iidx + 1;
            rsz = decl.alloc_size() + (
                std::size_t(arrs) * lf->ptr_base().alloc_size()
            );
            goto newdata;
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
            auto &fcd = tocdata(L, idx);
            if (fcd.decl.closure()) {
                cd = fcd.as<fdata>().cd;
                cdp = nullptr;
            }
        }
        void (*symp)() = nullptr;
        if (cdp) {
            std::memcpy(&symp, cdp, sizeof(symp));
        }
        make_cdata_func(
            L, symp, decl.function(), decl.type() == ast::C_BUILTIN_PTR, cd
        );
        if (!cdp && !cd) {
            tocdata(L, -1).as<fdata>().cd->fref = util::pun<int>(stor);
        }
    } else {
        auto &cd = newcdata(L, decl, rsz);
        void *dptr = nullptr;
        std::size_t msz = rsz;
        if (!cdp) {
            std::memset(cd.as_ptr(), 0, rsz);
            if (decl.type() == ast::C_BUILTIN_ARRAY) {
                auto *bval = static_cast<unsigned char *>(cd.as_ptr());
                dptr = bval + sizeof(ffi::scalar_stor_t);
                cd.as<void *>() = dptr;
                msz = rsz - sizeof(ffi::scalar_stor_t);
            } else {
                dptr = cd.as_ptr();
            }
        } else if (decl.type() == ast::C_BUILTIN_ARRAY) {
            std::size_t esz = (rsz - sizeof(ffi::scalar_stor_t)) / narr;
            /* the base of the alloated block */
            auto *bval = static_cast<unsigned char *>(cd.as_ptr());
            /* the array memory begins after the first ffi::scalar_stor_t */
            auto *val = bval + sizeof(ffi::scalar_stor_t);
            dptr = val;
            /* we can treat an array like a pointer, always */
            cd.as<void *>() = dptr;
            /* write initializers into the array part */
            for (std::size_t i = 0; i < narr; ++i) {
                std::memcpy(&val[i * esz], cdp, esz);
            }
            msz = rsz - sizeof(ffi::scalar_stor_t);
        } else {
            dptr = cd.as_ptr();
            std::memcpy(dptr, cdp, rsz);
        }
        /* perform aggregate initialization */
        from_lua_aggreg(L, decl, dptr, msz, ninits, iidx);
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
