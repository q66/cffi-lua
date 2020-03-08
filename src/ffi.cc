#include <limits>
#include <type_traits>
#include <algorithm>

#include "platform.hh"
#include "ffi.hh"

namespace ffi {

ffi_type *from_lua_type(lua_State *L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TBOOLEAN:
            return &ffi_type_uchar;
        case LUA_TNUMBER:
            return ffi_traits<lua_Number>::type();
        case LUA_TNIL:
        case LUA_TSTRING:
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TTHREAD:
        case LUA_TLIGHTUSERDATA:
            return &ffi_type_pointer;
        case LUA_TUSERDATA: {
            auto *cd = ffi::testcdata<char>(L, index);
            if (!cd) {
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

static ast::c_value *&get_auxptr(cdata<fdata> &fud) {
    return *reinterpret_cast<ast::c_value **>(&fud.val.args[1]);
}

void destroy_cdata(lua_State *L, cdata<char> &cd) {
    auto &fd = *reinterpret_cast<cdata<fdata> *>(&cd.decl);
    if (cd.gc_ref >= 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cd.gc_ref); /* the finalizer */
        lua_pushvalue(L, 1); /* the cdata */
        if (lua_pcall(L, 1, 0, 0)) {
            lua_pop(L, 1);
        }
    }
    if (cd.decl.closure() && fd.val.cd) {
        /* this is O(n) which sucks a little */
        fd.val.cd->refs.remove(&fd.val.cd);
    }
    switch (cd.decl.type()) {
        case ast::C_BUILTIN_FPTR:
        case ast::C_BUILTIN_FUNC: {
            if (!fd.decl.function().variadic()) {
                break;
            }
            delete[] reinterpret_cast<unsigned char *>(get_auxptr(fd));
        }
        default:
            break;
    }
    using T = ast::c_type;
    cd.decl.~T();
}

using signed_size_t = typename std::make_signed<size_t>::type;

static void cb_bind(ffi_cif *, void *ret, void *args[], void *data) {
    auto &fud = *static_cast<ffi::cdata<ffi::fdata> *>(data);
    auto &fun = fud.decl.function();
    auto &pars = fun.params();
    size_t nargs = pars.size();

    closure_data &cd = *fud.val.cd;
    lua_rawgeti(cd.L, LUA_REGISTRYINDEX, cd.fref);
    for (size_t i = 0; i < nargs; ++i) {
        to_lua(cd.L, pars[i].type(), args[i], RULE_PASS);
    }
    lua_call(cd.L, nargs, 1);

    if (fun.result().type() != ast::C_BUILTIN_VOID) {
        ast::c_value stor;
        size_t rsz;
        void *rp = from_lua(
            cd.L, fun.result(), &stor, -1, rsz, RULE_RET
        );
        memcpy(ret, rp, rsz);
    }
}

/* this initializes a non-vararg cif with the given number of arguments
 * for variadics, this is initialized once for zero args, and then handled
 * dynamically before every call
 */
static bool prepare_cif(cdata<fdata> &fud, size_t nargs) {
    auto &func = fud.decl.function();

    ffi_type **targs = reinterpret_cast<ffi_type **>(&fud.val.args[nargs + 1]);
    for (size_t i = 0; i < nargs; ++i) {
        targs[i] = func.params()[i].libffi_type();
    }

    return (ffi_prep_cif(
        &fud.val.cif, FFI_DEFAULT_ABI, nargs,
        func.result().libffi_type(), targs
    ) == FFI_OK);
}

static void make_cdata_func(
    lua_State *L, void (*funp)(), ast::c_function const &func, int cbt,
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
     *         ast::c_value val1; // lua arg1
     *         ast::c_value val2; // lua arg2
     *         ast::c_value valN; // lua argN
     *         ast::c_value valR; // lua ret
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
     *         ast::c_value valR; // lua ret
     *         void *aux; // vals + types + args like above, but dynamic
     *     } val;
     * }
     */
    auto &fud = newcdata<fdata>(
        L, ast::c_type{&func, 0, cbt, funp == nullptr},
        func.variadic() ? (sizeof(ast::c_value) + sizeof(void *)) : (
            sizeof(ast::c_value[1 + nargs]) + sizeof(void *[2 * nargs])
        )
    );
    fud.val.sym = funp;

    if (func.variadic()) {
        get_auxptr(fud) = nullptr;
    }

    if (!ffi::prepare_cif(fud, !func.variadic() ? nargs : 0)) {
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
        cd = new closure_data{};
        /* allocate a closure in it */
        cd->closure = static_cast<ffi_closure *>(ffi_closure_alloc(
            sizeof(ffi_closure), reinterpret_cast<void **>(&fud.val.sym)
        ));
        if (!cd->closure) {
            delete cd;
            luaL_error(
                L, "failed allocating callback for '%s'",
                func.serialize().c_str()
            );
        }
        /* XXX: we're using the cif here, but the cif may be reinitialized
         * for varargs; it doesn't seem like the closure code is using any
         * of the stuff that changes for varargs, and only reads the ABI
         * during this call (which is fine) in general, so it should be
         * fine, if it's not we'd have to reinitialize the closures
         * every time and that'd be a pain in the ass
         */
        if (ffi_prep_closure_loc(
            cd->closure, &fud.val.cif, cb_bind, &fud,
            reinterpret_cast<void *>(fud.val.sym)
        ) != FFI_OK) {
            delete cd;
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

    auto &auxptr = get_auxptr(fud);
    if (auxptr && (nargs > size_t(fud.aux))) {
        delete auxptr;
        auxptr = nullptr;
    }
    if (!auxptr) {
        auxptr = reinterpret_cast<ast::c_value *>(new unsigned char[
            sizeof(ast::c_value) * nargs + sizeof(void *) * nargs * 2
        ]);
        fud.aux = int(nargs);
    }

    ffi_type **targs = reinterpret_cast<ffi_type **>(&auxptr[nargs]);
    for (size_t i = 0; i < fargs; ++i) {
        targs[i] = func.params()[i].libffi_type();
    }
    for (size_t i = fargs; i < nargs; ++i) {
        targs[i] = from_lua_type(L, i + 2);
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
    size_t fargs = nargs;
    size_t targs = fargs;

    ast::c_value *pvals = fud.val.args;
    void **vals;
    void *rval;

    if (func.variadic()) {
        --fargs;
        targs = std::max(largs, fargs);
        if (!prepare_cif_var(L, fud, targs, fargs)) {
            luaL_error(L, "unexpected failure setting up '%s'", func.name());
        }
        rval = &pvals[0];
        auto *auxp = get_auxptr(fud);
        pvals = auxp;
        vals = &reinterpret_cast<void **>(&auxp[targs])[targs];
    } else {
        rval = &pvals[nargs];
        vals = &reinterpret_cast<void **>(&pvals[nargs + 1])[nargs];
    }

    /* fixed args */
    for (size_t i = 0; i < fargs; ++i) {
        size_t rsz;
        vals[i] = from_lua(
            L, pdecls[i].type(), &pvals[i], i + 2, rsz, RULE_PASS
        );
    }
    /* variable args */
    for (size_t i = fargs; i < targs; ++i) {
        size_t rsz;
        vals[i] = from_lua(
            L, ast::from_lua_type(L, i + 2), &pvals[i], i + 2, rsz, RULE_PASS
        );
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
    lua_State *L, ast::c_type const &tp, void *value, bool lossy
) {
    /* assumes radix-2 floats... */
    if ((
        std::numeric_limits<T>::digits <=
        std::numeric_limits<lua_Number>::digits
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
    lua_State *L, ast::c_type const &tp, void *value, bool lossy
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
    lua_State *L, ast::c_type const &tp, void *value, int rule, bool lossy
) {
    switch (ast::c_builtin(tp.type())) {
        /* no retval */
        case ast::C_BUILTIN_VOID:
            return 0;
        /* convert to lua boolean */
        case ast::C_BUILTIN_BOOL:
            lua_pushboolean(L, *static_cast<bool *>(value));
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
        case ast::C_BUILTIN_INT8:
            return push_int<int8_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_UINT8:
            return push_int<uint8_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_INT16:
            return push_int<int16_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_UINT16:
            return push_int<uint16_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_INT32:
            return push_int<int32_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_UINT32:
            return push_int<uint32_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_WCHAR:
            return push_int<wchar_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_CHAR16:
            return push_int<char16_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_CHAR32:
            return push_int<char16_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_LONG:
            return push_int<long>(L, tp, value, lossy);
        case ast::C_BUILTIN_ULONG:
            return push_int<unsigned long>(L, tp, value, lossy);
        case ast::C_BUILTIN_LLONG:
            return push_int<long long>(L, tp, value, lossy);
        case ast::C_BUILTIN_ULLONG:
            return push_int<unsigned long long>(L, tp, value, lossy);
        case ast::C_BUILTIN_INT64:
            return push_int<int64_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_UINT64:
            return push_int<uint64_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_SIZE:
            return push_int<size_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_SSIZE:
            return push_int<signed_size_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_INTPTR:
            return push_int<intptr_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_UINTPTR:
            return push_int<uintptr_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_PTRDIFF:
            return push_int<ptrdiff_t>(L, tp, value, lossy);
        case ast::C_BUILTIN_TIME:
            if (!std::numeric_limits<time_t>::is_integer) {
                return push_flt<time_t>(L, tp, value, lossy);
            }
            return push_int<time_t>(L, tp, value, lossy);

        case ast::C_BUILTIN_REF:
            if (rule == RULE_CONV) {
                /* for this rule, dereference and pass that */
                return to_lua(
                    L, tp.ptr_base(), *reinterpret_cast<void **>(value),
                    RULE_CONV, lossy
                );
            }
            goto ptr_ref;

        ptr_ref:
        case ast::C_BUILTIN_VA_LIST:
        case ast::C_BUILTIN_PTR:
            /* pointers should be handled like large cdata, as they need
             * to be represented as userdata objects on lua side either way
             */
            newcdata<void *>(L, tp).val = *reinterpret_cast<void **>(value);
            return 1;

        case ast::C_BUILTIN_FPTR:
            make_cdata_func(
                L, reinterpret_cast<ast::c_value *>(value)->fptr,
                tp.function(), ast::C_BUILTIN_FPTR, nullptr
            );
            return 1;

        case ast::C_BUILTIN_ENUM:
            /* TODO: large enums */
            return push_int<int>(L, tp, value, lossy);

        case ast::C_BUILTIN_STRUCT: {
            auto sz = tp.alloc_size();
            auto &cd = newcdata(L, tp, sz);
            memcpy(&cd.val, value, sz);
            return 1;
        }

        case ast::C_BUILTIN_FUNC:
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

void *from_lua(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    size_t &dsz, int rule
) {
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
        case LUA_TBOOLEAN: {
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
                case ast::C_BUILTIN_WCHAR:
                    return write_int<wchar_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_CHAR16:
                    return write_int<char16_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_CHAR32:
                    return write_int<char32_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_INT8:
                    return write_int<int8_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_UINT8:
                    return write_int<uint8_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_INT16:
                    return write_int<int16_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_UINT16:
                    return write_int<uint16_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_INT32:
                    return write_int<int32_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_UINT32:
                    return write_int<uint32_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_INT64:
                    return write_int<int64_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_UINT64:
                    return write_int<uint64_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_SIZE:
                    return write_int<size_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_SSIZE:
                    return write_int<signed_size_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_INTPTR:
                    return write_int<intptr_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_UINTPTR:
                    return write_int<uintptr_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_PTRDIFF:
                    return write_int<ptrdiff_t>(L, index, stor, dsz);
                case ast::C_BUILTIN_TIME:
                    if (!std::numeric_limits<time_t>::is_integer) {
                        return write_flt<time_t>(L, index, stor, dsz);
                    }
                    return write_int<time_t>(L, index, stor, dsz);

                case ast::C_BUILTIN_ENUM:
                    /* TODO: large enums */
                    return write_int<int>(L, index, stor, dsz);

                case ast::C_BUILTIN_VOID:
                case ast::C_BUILTIN_PTR:
                case ast::C_BUILTIN_REF:
                case ast::C_BUILTIN_FPTR:
                case ast::C_BUILTIN_STRUCT:
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
            break;
        }
        case LUA_TSTRING:
            if (
                (tp.type() == ast::C_BUILTIN_PTR) &&
                (tp.ptr_base().type() == ast::C_BUILTIN_CHAR) &&
                (tp.ptr_base().cv() & ast::C_CV_CONST)) {
            } {
                dsz = sizeof(char const *);
                return &(
                    *static_cast<char const **>(stor) = lua_tostring(L, index)
                );
            }
            luaL_error(
                L, "cannot convert 'string' to '%s'", tp.serialize().c_str()
            );
            break;
        case LUA_TUSERDATA:
            if (iscdata(L, index)) {
                /* special handling for cdata */
                auto &cd = *lua::touserdata<ffi::cdata<ast::c_value>>(L, index);
                if (!cd.decl.converts_to(tp)) {
                    luaL_error(
                        L, "cannot convert '%s' to '%s'",
                        cd.decl.serialize().c_str(),
                        tp.serialize().c_str()
                    );
                }
                if (tp.type() == ast::C_BUILTIN_REF) {
                    if (
                        (cd.decl.type() != ast::C_BUILTIN_PTR) &&
                        (cd.decl.type() != ast::C_BUILTIN_REF)
                    ) {
                        dsz = sizeof(void *);
                        return &(*static_cast<void **>(stor) = &cd.val);
                    }
                }
                dsz = cd.val_sz;
                return &cd.val;
            } else if (tp.type() == ast::C_BUILTIN_PTR) {
                /* unqualified void pointer converts to any pointer in C */
                dsz = sizeof(void *);
                return &(
                    *static_cast<void **>(stor) = lua_touserdata(L, index)
                );
            } else if (isctype(L, index)) {
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
            luaL_error(L, "table initializers not yet implemented");
            break;
        case LUA_TFUNCTION:
            if (tp.type() != ast::C_BUILTIN_FPTR) {
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
                L, "'%s' cannot be used in FFI",
                lua_typename(L, lua_type(L, index))
            );
            break;
    }
    assert(false);
    return nullptr;
}

void get_global(lua_State *L, lib::handle dl, const char *sname) {
    auto &ds = ast::decl_store::get_main(L);
    auto const *decl = ds.lookup(sname);

    auto tp = ast::c_object_type::INVALID;
    if (decl) {
        tp = decl->obj_type();
    }

    switch (tp) {
        case ast::c_object_type::FUNCTION: {
            auto funp = lib::get_func(dl, sname);
            if (!funp) {
                luaL_error(L, "undefined symbol: %s", sname);
            }
            make_cdata_func(
                L, funp, decl->as<ast::c_function>(),
                ast::C_BUILTIN_FUNC, nullptr
            );
            return;
        }
        case ast::c_object_type::VARIABLE: {
            void *symp = lib::get_var(dl, sname);
            if (!symp) {
                luaL_error(L, "undefined symbol: %s", sname);
            }
            to_lua(
                L, decl->as<ast::c_variable>().type(), symp, RULE_RET
            );
            return;
        }
        case ast::c_object_type::CONSTANT: {
            auto &cd = decl->as<ast::c_constant>();
            to_lua(
                L, cd.type(), const_cast<ast::c_value *>(&cd.value()),
                RULE_CONV
            );
            return;
        }
        default:
            luaL_error(
                L, "missing declaration for symbol '%s'", sname
            );
            return;
    }
}

void set_global(lua_State *L, lib::handle dl, char const *sname, int idx) {
    auto &ds = ast::decl_store::get_main(L);
    auto const *decl = ds.lookup(sname);
    if (!decl) {
        luaL_error(L, "missing declaration for symbol '%s'", decl->name());
        return;
    }
    if (decl->obj_type() != ast::c_object_type::VARIABLE) {
        luaL_error(L, "symbol '%s' is not mutable", decl->name());
    }

    void *symp = lib::get_var(dl, sname);
    if (!symp) {
        luaL_error(L, "undefined symbol: %s", sname);
        return;
    }

    size_t rsz;
    from_lua(
        L, decl->as<ast::c_variable>().type(), symp, idx, rsz, ffi::RULE_CONV
    );
}

void make_cdata(lua_State *L, ast::c_type const &decl, int rule, int idx) {
    switch (decl.type()) {
        case ast::C_BUILTIN_FUNC:
            luaL_error(L, "invalid C type");
            break;
        default:
            break;
    }
    ast::c_value stor{};
    void *cdp = nullptr;
    size_t rsz = 0;
    if (lua_type(L, idx) != LUA_TNONE) {
        cdp = ffi::from_lua(L, decl, &stor, idx, rsz, rule);
    } else {
        rsz = decl.alloc_size();
    }
    if (decl.type() == ast::C_BUILTIN_FPTR) {
        ffi::closure_data *cd = nullptr;
        if (cdp && ffi::iscdata(L, idx)) {
            /* special handling for closures */
            auto &fcd = ffi::tocdata<ffi::fdata>(L, idx);
            if (fcd.decl.closure()) {
                cd = fcd.val.cd;
                cdp = nullptr;
            }
        }
        using FP = void (*)();
        make_cdata_func(
            L, cdp ? *reinterpret_cast<FP *>(cdp) : nullptr,
            decl.function(), decl.type(), cd
        );
        if (!cdp && !cd) {
            ffi::tocdata<ffi::fdata>(L, -1).val.cd->fref = stor.i;
        }
    } else {
        auto &cd = ffi::newcdata(L, decl, rsz);
        if (!cdp) {
            memset(&cd.val, 0, rsz);
        } else {
            memcpy(&cd.val, cdp, rsz);
        }
    }
}

} /* namespace ffi */
