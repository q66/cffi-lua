#include <limits>
#include <type_traits>

#include "ffi.hh"

namespace ffi {

static void make_cdata_func(
    lua_State *L, lib::handle dl, ast::c_function const &func, char const *name
) {
    auto funp = lib::get_func(dl, name);
    if (!funp) {
        luaL_error(L, "undefined symbol: %s", name);
    }

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
    auto *fud = lua::newuserdata<ffi::cdata<ffi::fdata>>(
        L, sizeof(ast::c_value[1 + nargs]) + sizeof(void *[2 * nargs])
    );
    luaL_setmetatable(L, "cffi_cdata_handle");

    new (&fud->decl) ast::c_type{&func, 0, ast::C_BUILTIN_FUNC};
    fud->val.sym = funp;

    if (!ffi::prepare_cif(*fud)) {
        luaL_error(
            L, "unexpected failure setting up '%s'", func.name.c_str()
        );
    }
}

static void make_cdata_var(
    lua_State *L, lib::handle dl, ast::c_variable const &var, char const *name
) {
    void *symp = lib::get_var(dl, name);
    if (!symp) {
        luaL_error(L, "undefined symbol: %s", name);
    }
    lua_push_cdata(L, var.type(), symp);
}

void make_cdata(
    lua_State *L, lib::handle dl, ast::c_object const *obj, char const *name
) {
    auto tp = ast::c_object_type::INVALID;
    if (obj) {
        tp = obj->obj_type();
    }
    switch (tp) {
        case ast::c_object_type::FUNCTION:
            make_cdata_func(L, dl, obj->as<ast::c_function>(), name);
            return;
        case ast::c_object_type::VARIABLE:
            make_cdata_var(L, dl, obj->as<ast::c_variable>(), name);
            return;
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
        vals[i] = lua_check_cdata(
            L, pdecls[i].type(), &pvals[i], i + 2
        );
    }

    ffi_call(&fud.val.cif, fud.val.sym, &pvals[nargs], vals);
    return lua_push_cdata(L, func.result(), &pvals[nargs]);
}

template<typename T>
static inline void push_int(lua_State *L, void *value) {
    using U = T *;
    lua_pushinteger(L, lua_Integer(*U(value)));
}

int lua_push_cdata(lua_State *L, ast::c_type const &tp, void *value) {
    switch (ast::c_builtin(tp.type())) {
        /* no retval */
        case ast::C_BUILTIN_VOID:
            return 0;
        /* convert to lua boolean */
        case ast::C_BUILTIN_BOOL:
            lua_pushboolean(L, *static_cast<unsigned char *>(value));
            return 1;
        /* convert to lua number */
        case ast::C_BUILTIN_FLOAT:
            lua_pushnumber(L, lua_Number(*static_cast<float *>(value)));
            return 1;
        case ast::C_BUILTIN_DOUBLE:
            lua_pushnumber(L, lua_Number(*static_cast<double *>(value)));
            return 1;
        case ast::C_BUILTIN_LDOUBLE:
            lua_pushnumber(L, lua_Number(*static_cast<long double *>(value)));
            return 1;
        case ast::C_BUILTIN_CHAR:
            push_int<char>(L, value); return 1;
        case ast::C_BUILTIN_SCHAR:
            push_int<signed char>(L, value); return 1;
        case ast::C_BUILTIN_UCHAR:
            push_int<unsigned char>(L, value); return 1;
        case ast::C_BUILTIN_SHORT:
            push_int<short>(L, value); return 1;
        case ast::C_BUILTIN_USHORT:
            push_int<unsigned short>(L, value); return 1;
        case ast::C_BUILTIN_INT:
            push_int<int>(L, value); return 1;
        case ast::C_BUILTIN_UINT:
            push_int<unsigned int>(L, value); return 1;
        case ast::C_BUILTIN_INT8:
            push_int<int8_t>(L, value); return 1;
        case ast::C_BUILTIN_UINT8:
            push_int<uint8_t>(L, value); return 1;
        case ast::C_BUILTIN_INT16:
            push_int<int16_t>(L, value); return 1;
        case ast::C_BUILTIN_UINT16:
            push_int<uint16_t>(L, value); return 1;
        case ast::C_BUILTIN_INT32:
            push_int<int32_t>(L, value); return 1;
        case ast::C_BUILTIN_UINT32:
            push_int<uint32_t>(L, value); return 1;
        case ast::C_BUILTIN_WCHAR:
            push_int<wchar_t>(L, value); return 1;
        case ast::C_BUILTIN_CHAR16:
            push_int<char16_t>(L, value); return 1;
        case ast::C_BUILTIN_CHAR32:
            push_int<char16_t>(L, value); return 1;
        case ast::C_BUILTIN_LONG:
        case ast::C_BUILTIN_ULONG:
        case ast::C_BUILTIN_LLONG:
        case ast::C_BUILTIN_ULLONG:
        case ast::C_BUILTIN_INT64:
        case ast::C_BUILTIN_UINT64:
        case ast::C_BUILTIN_SIZE:
        case ast::C_BUILTIN_SSIZE:
        case ast::C_BUILTIN_INTPTR:
        case ast::C_BUILTIN_UINTPTR:
        case ast::C_BUILTIN_PTRDIFF:
        case ast::C_BUILTIN_TIME:
        case ast::C_BUILTIN_STRUCT:
        case ast::C_BUILTIN_ENUM:
        case ast::C_BUILTIN_FUNC:
        case ast::C_BUILTIN_FPTR:
            luaL_error(L, "NYI"); return 0;
        case ast::C_BUILTIN_PTR: {
            /* pointers should be handled like large cdata, as they need
             * to be represented as userdata objects on lua side either way
             */
            auto *fud = lua::newuserdata<ffi::cdata<void *>>(L);
            new (&fud->decl) ast::c_type{tp};
            fud->val = *reinterpret_cast<void **>(value);
            luaL_setmetatable(L, "cffi_cdata_handle");
            return 1;
        }

        case ast::C_BUILTIN_INVALID:
            break;
    }

    luaL_error(L, "unexpected error: unhandled type %d", tp.type());
    return 0;
}

template<typename T>
static inline void *write_int(lua_State *L, int index, void *stor) {
    lua_Integer v = lua_tointeger(L, index);
    *static_cast<T *>(stor) = T(v);
    return stor;
}

void *lua_check_cdata(
    lua_State *L, ast::c_type const &tp, ast::c_value *stor, int index
) {
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            switch (tp.type()) {
                case ast::C_BUILTIN_PTR:
                    return &(stor->ptr = nullptr);
                default:
                    luaL_error(
                        L, "cannot convert 'nil' to '%s'",
                        tp.serialize().c_str()
                    );
                    break;
            }
            break;
        case LUA_TBOOLEAN:
            switch (tp.type()) {
                case ast::C_BUILTIN_BOOL:
                    stor->b = lua_toboolean(L, index);
                    return &stor->b;
                default:
                    luaL_error(
                        L, "cannot convert 'boolean' to '%s'",
                        tp.serialize().c_str()
                    );
                    break;
            }
            break;
        case LUA_TNUMBER:
            switch (tp.type()) {
                case ast::C_BUILTIN_FLOAT:
                    return &(stor->f = float(lua_tonumber(L, index)));
                case ast::C_BUILTIN_DOUBLE:
                    return &(stor->d = double(lua_tonumber(L, index)));
                case ast::C_BUILTIN_CHAR:
                    return write_int<char>(L, index, &stor->c);
                case ast::C_BUILTIN_SCHAR:
                    return write_int<signed char>(L, index, &stor->sc);
                case ast::C_BUILTIN_UCHAR:
                    return write_int<unsigned char>(L, index, &stor->uc);
                case ast::C_BUILTIN_SHORT:
                    return write_int<short>(L, index, &stor->s);
                case ast::C_BUILTIN_USHORT:
                    return write_int<unsigned short>(L, index, &stor->us);
                case ast::C_BUILTIN_INT:
                    return write_int<int>(L, index, &stor->i);
                case ast::C_BUILTIN_UINT:
                    return write_int<unsigned int>(L, index, &stor->u);
                case ast::C_BUILTIN_LONG:
                    return write_int<long>(L, index, &stor->l);
                case ast::C_BUILTIN_ULONG:
                    return write_int<unsigned long>(L, index, &stor->ul);
                case ast::C_BUILTIN_LLONG:
                    return write_int<long long>(L, index, &stor->ll);
                case ast::C_BUILTIN_ULLONG:
                    return write_int<unsigned long long>(L, index, &stor->ull);
                case ast::C_BUILTIN_WCHAR:
                    return write_int<wchar_t>(L, index, &stor->w);
                case ast::C_BUILTIN_CHAR16:
                    return write_int<char16_t>(L, index, &stor->c16);
                case ast::C_BUILTIN_CHAR32:
                    return write_int<char32_t>(L, index, &stor->c32);
                case ast::C_BUILTIN_INT8:
                    return write_int<int8_t>(L, index, &stor->i8);
                case ast::C_BUILTIN_UINT8:
                    return write_int<uint8_t>(L, index, &stor->u8);
                case ast::C_BUILTIN_INT16:
                    return write_int<int16_t>(L, index, &stor->i16);
                case ast::C_BUILTIN_UINT16:
                    return write_int<uint16_t>(L, index, &stor->u16);
                case ast::C_BUILTIN_INT32:
                    return write_int<int32_t>(L, index, &stor->i32);
                case ast::C_BUILTIN_UINT32:
                    return write_int<uint32_t>(L, index, &stor->u32);
                case ast::C_BUILTIN_INT64:
                    return write_int<int64_t>(L, index, &stor->i64);
                case ast::C_BUILTIN_UINT64:
                    return write_int<uint64_t>(L, index, &stor->u64);
                case ast::C_BUILTIN_SIZE:
                    return write_int<size_t>(L, index, &stor->sz);
                case ast::C_BUILTIN_SSIZE:
                    return write_int<ssize_t>(L, index, &stor->ssz);
                case ast::C_BUILTIN_INTPTR:
                    return write_int<intptr_t>(L, index, &stor->ip);
                case ast::C_BUILTIN_UINTPTR:
                    return write_int<uintptr_t>(L, index, &stor->uip);
                case ast::C_BUILTIN_PTRDIFF:
                    return write_int<ptrdiff_t>(L, index, &stor->pd);
                default:
                    luaL_error(
                        L, "cannot convert 'number' to '%s'",
                        tp.serialize().c_str()
                    );
                    break;
            }
            break;
        case LUA_TSTRING:
            switch (tp.type()) {
                case ast::C_BUILTIN_PTR:
                    return &(stor->str = lua_tostring(L, index));
                default:
                    luaL_error(
                        L, "cannot convert 'string' to '%s'",
                        tp.serialize().c_str()
                    );
                    break;
            }
            break;
        case LUA_TUSERDATA:
        case LUA_TLIGHTUSERDATA:
            switch (tp.type()) {
                case ast::C_BUILTIN_PTR:
                    if (luaL_testudata(L, index, "cffi_cdata_handle")) {
                        /* special handling for cdata */
                        /* FIXME: check type conversions... */
                        return &(stor->ptr = lua::touserdata<
                            ffi::cdata<void *>
                        >(L, index)->val);
                    } else {
                        return &(stor->ptr = lua_touserdata(L, index));
                    }
                default:
                    luaL_error(
                        L, "cannot convert 'string' to '%s'",
                        tp.serialize().c_str()
                    );
                    break;
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
    return nullptr;
}

} /* namespace ffi */
