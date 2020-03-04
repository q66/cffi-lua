#include <limits>
#include <type_traits>

#include "platform.hh"
#include "ffi.hh"

namespace ffi {

using signed_size_t = typename std::make_signed<size_t>::type;

static void cb_bind(ffi_cif *, void *ret, void *args[], void *data) {
    auto &fud = *static_cast<ffi::cdata<ffi::fdata> *>(data);
    auto &fun = fud.decl.function();
    auto &pars = fun.params();
    size_t nargs = pars.size();

    closure_data &cd = *fud.val.cd;
    lua_rawgeti(cd.L, LUA_REGISTRYINDEX, cd.fref);
    for (size_t i = 0; i < nargs; ++i) {
        lua_push_cdata(cd.L, pars[i].type(), args[i]);
    }
    lua_call(cd.L, nargs, 1);

    if (fun.result().type() != ast::C_BUILTIN_VOID) {
        ast::c_value stor;
        size_t rsz;
        void *rp = lua_check_cdata(cd.L, fun.result(), &stor, -1, rsz);
        memcpy(ret, rp, rsz);
    }
}

void make_cdata_func(
    lua_State *L, void (*funp)(), ast::c_function const &func, int cbt,
    closure_data *cd
) {
    size_t nargs = func.params().size();

    /* MEMORY LAYOUT:
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
     */
    auto &fud = newcdata<fdata>(
        L, ast::c_type{&func, 0, cbt, funp == nullptr},
        sizeof(ast::c_value[1 + nargs]) + sizeof(void *[2 * nargs])
    );
    fud.val.sym = funp;

    if (!ffi::prepare_cif(fud)) {
        luaL_error(
            L, "unexpected failure setting up '%s'", func.name.c_str()
        );
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

void make_cdata(
    lua_State *L, lib::handle dl, ast::c_object const *obj, char const *name
) {
    auto tp = ast::c_object_type::INVALID;
    if (obj) {
        tp = obj->obj_type();
    }
    switch (tp) {
        case ast::c_object_type::FUNCTION: {
            auto funp = lib::get_func(dl, name);
            if (!funp) {
                luaL_error(L, "undefined symbol: %s", name);
            }
            make_cdata_func(L, funp, obj->as<ast::c_function>());
            return;
        }
        case ast::c_object_type::VARIABLE: {
            void *symp = lib::get_var(dl, name);
            if (!symp) {
                luaL_error(L, "undefined symbol: %s", name);
            }
            lua_push_cdata(L, obj->as<ast::c_variable>().type(), symp);
            return;
        }
        case ast::c_object_type::CONSTANT: {
            auto &cd = obj->as<ast::c_constant>();
            lua_push_cdata(
                L, cd.type(), const_cast<ast::c_value *>(&cd.value())
            );
            return;
        }
        default:
            luaL_error(
                L, "missing declaration for symbol '%s'", obj->name.c_str()
            );
            return;
    }
}

bool prepare_cif(cdata<fdata> &fud) {
    auto &func = fud.decl.function();
    size_t nargs = func.params().size();

    ffi_type **targs = reinterpret_cast<ffi_type **>(&fud.val.args[nargs + 1]);
    for (size_t i = 0; i < nargs; ++i) {
        targs[i] = func.params()[i].libffi_type();
    }

    if (ffi_prep_cif(
        &fud.val.cif, FFI_DEFAULT_ABI, nargs,
        func.result().libffi_type(), targs
    ) != FFI_OK) {
        return false;
    }

    return true;
}

int call_cif(cdata<fdata> &fud, lua_State *L) {
    auto &func = fud.decl.function();
    auto &pdecls = func.params();

    size_t nargs = pdecls.size();

    auto *pvals = fud.val.args;
    void **vals = &reinterpret_cast<void **>(&pvals[nargs + 1])[nargs];

    for (size_t i = 0; i < pdecls.size(); ++i) {
        size_t rsz;
        vals[i] = lua_check_cdata(
            L, pdecls[i].type(), &pvals[i], i + 2, rsz
        );
    }

    ffi_call(&fud.val.cif, fud.val.sym, &pvals[nargs], vals);
    void *retp = &pvals[nargs];
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
    auto rsz = func.result().libffi_type()->size;
    if (rsz < sizeof(ffi_arg)) {
        auto *p = static_cast<unsigned char *>(retp);
        retp = p + sizeof(ffi_arg) - rsz;
    }
#endif
    return lua_push_cdata(L, func.result(), retp);
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
    auto &cd = newcdata<noval>(L, tp, sizeof(T));
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
    auto &cd = newcdata<noval>(L, tp, sizeof(T));
    memcpy(&cd.val, value, sizeof(T));
    return 1;
}

int lua_push_cdata(
    lua_State *L, ast::c_type const &tp, void *value, bool lossy
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

        case ast::C_BUILTIN_PTR:
        case ast::C_BUILTIN_REF:
            /* pointers should be handled like large cdata, as they need
             * to be represented as userdata objects on lua side either way
             */
            newcdata<ptrval>(L, tp).val.ptr =
                reinterpret_cast<ptrval *>(value)->ptr;
            return 1;

        case ast::C_BUILTIN_FPTR:
            make_cdata_func(
                L, reinterpret_cast<ast::c_value *>(value)->fptr,
                tp.function(), ast::C_BUILTIN_FPTR
            );
            return 1;

        case ast::C_BUILTIN_ENUM:
            /* TODO: large enums */
            return push_int<int>(L, tp, value, lossy);

        case ast::C_BUILTIN_STRUCT:
            luaL_error(L, "NYI"); return 0;

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

void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, void *stor, int index,
    size_t &dsz
) {
    auto vtp = lua_type(L, index);
    switch (vtp) {
        case LUA_TNIL:
            switch (tp.type()) {
                case ast::C_BUILTIN_PTR:
                    dsz = sizeof(void *);
                    return &(*static_cast<void **>(stor) = nullptr);
                default:
                    luaL_error(
                        L, "cannot convert 'nil' to '%s'",
                        tp.serialize().c_str()
                    );
                    break;
            }
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
            luaL_error(L, "callbacks not yet implemented");
            break;
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

} /* namespace ffi */
