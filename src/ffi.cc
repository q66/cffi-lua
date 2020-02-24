#include <limits>
#include <type_traits>

#include "ffi.hh"

namespace ffi {

bool prepare_cif(cdata<fdata> &fud) {
    auto &func = fud.decl->as<ast::c_function>();
    size_t nargs = func.params().size();

    ffi_type **targs = reinterpret_cast<ffi_type **>(&fud.val.args[nargs + 1]);
    for (size_t i = 0; i < nargs; ++i) {
        targs[i] = ffi::get_ffi_type(func.params()[i].type());
    }

    if (ffi_prep_cif(
        &fud.val.cif, FFI_DEFAULT_ABI, nargs,
        ffi::get_ffi_type(func.result()), targs
    ) != FFI_OK) {
        return false;
    }

    return true;
}

void call_cif(cdata<fdata> &fud, lua_State *L) {
    auto &func = fud.decl->as<ast::c_function>();
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
    lua_push_cdata(L, func.result(), &pvals[nargs]);
}

template<typename T>
static inline bool use_ffi_signed(int cv) {
    if (cv & ast::C_CV_SIGNED) {
        return true;
    }
    if (cv & ast::C_CV_UNSIGNED) {
        return false;
    }
    return std::numeric_limits<T>::is_signed;
}

ffi_type *get_ffi_type(ast::c_type const &tp) {
    int cv = tp.cv();

#define INT_CASE(bname, rtype, ftype) \
    case ast::C_BUILTIN_##bname: \
        if (use_ffi_signed<rtype>(cv)) { \
            return &ffi_type_s##ftype; \
        } else { \
            return &ffi_type_u##ftype; \
        }

#define INT_CASE64(bname, rtype) \
    case ast::C_BUILTIN_##bname: \
        if (sizeof(rtype) == 8) { \
            if (cv & ast::C_CV_SIGNED) { \
                return &ffi_type_sint64; \
            } else { \
                return &ffi_type_uint64; \
            } \
        } else if (sizeof(rtype) == 4) { \
            if (cv & ast::C_CV_SIGNED) { \
                return &ffi_type_sint32; \
            } else { \
                return &ffi_type_uint32; \
            } \
        } else if (sizeof(rtype) == 2) { \
            if (cv & ast::C_CV_SIGNED) { \
                return &ffi_type_sint16; \
            } else { \
                return &ffi_type_uint16; \
            } \
        } else { \
            if (cv & ast::C_CV_SIGNED) { \
                return &ffi_type_sint8; \
            } else { \
                return &ffi_type_uint8; \
            } \
        }

    switch (tp.type()) {
        case ast::C_BUILTIN_PTR:
            return &ffi_type_pointer;

        INT_CASE(CHAR, char, char)
        INT_CASE(SHORT, short, short)
        INT_CASE(INT, int, int)
        INT_CASE(LONG, long, long)
        INT_CASE(LLONG, int64_t, int64)
    
        INT_CASE(INT8, int8_t, int8)
        INT_CASE(INT16, int16_t, int16)
        INT_CASE(INT32, int32_t, int32)
        INT_CASE(INT64, int64_t, int64)

        INT_CASE64(SIZE, size_t)
        INT_CASE64(INTPTR, intptr_t)

        case ast::C_BUILTIN_TIME:
            /* FIXME: time_t may be represented in other ways too */
            if (sizeof(time_t) == 8) {
                return &ffi_type_sint64;
            } else {
                return &ffi_type_sint32;
            }

        case ast::C_BUILTIN_FLOAT:
            return &ffi_type_float;
        case ast::C_BUILTIN_DOUBLE:
            return &ffi_type_double;
        case ast::C_BUILTIN_LDOUBLE:
            /* FIXME: this may not be defined */
            return &ffi_type_longdouble;

        case ast::C_BUILTIN_BOOL:
            /* hmm... */
            return &ffi_type_uchar;

        default:
            break;
    }

#undef INT_CASE
#undef INT_CASE64

    /* TODO: custom types */
    return &ffi_type_sint;
}

template<typename T>
static inline void push_int(lua_State *L, int cv, void *value) {
    if (use_ffi_signed<T>(cv)) {
        using U = typename std::make_signed<T>::type *;
        lua_pushinteger(L, lua_Integer(*U(value)));
    } else {
        using U = typename std::make_unsigned<T>::type *;
        lua_pushinteger(L, lua_Integer(*U(value)));
    }
}

void lua_push_cdata(lua_State *L, ast::c_type const &tp, void *value) {
    switch (tp.type()) {
        /* convert to lua boolean */
        case ast::C_BUILTIN_BOOL:
            lua_pushboolean(L, *static_cast<unsigned char *>(value));
            return;
        /* convert to lua number */
        case ast::C_BUILTIN_FLOAT:
            lua_pushnumber(L, lua_Number(*static_cast<float *>(value)));
            return;
        case ast::C_BUILTIN_DOUBLE:
            lua_pushnumber(L, lua_Number(*static_cast<double *>(value)));
            return;
        case ast::C_BUILTIN_LDOUBLE:
            lua_pushnumber(L, lua_Number(*static_cast<long double *>(value)));
            return;
        case ast::C_BUILTIN_CHAR:
            push_int<char>(L, tp.cv(), value); return;
        case ast::C_BUILTIN_SHORT:
            push_int<short>(L, tp.cv(), value); return;
        case ast::C_BUILTIN_INT:
            push_int<int>(L, tp.cv(), value); return;
        case ast::C_BUILTIN_INT8:
            push_int<int8_t>(L, tp.cv(), value); return;
        case ast::C_BUILTIN_INT16:
            push_int<int16_t>(L, tp.cv(), value); return;
        case ast::C_BUILTIN_INT32:
            push_int<int32_t>(L, tp.cv(), value); return;
        case ast::C_BUILTIN_LONG:
        case ast::C_BUILTIN_INT64:
        case ast::C_BUILTIN_SIZE:
        case ast::C_BUILTIN_INTPTR:
        case ast::C_BUILTIN_TIME:
            luaL_error(L, "NYI"); return;
        default:
            /* pointers should be handled like large cdata, as they need
             * to be represented as userdata objects on lua side either way
             */
            luaL_error(L, "value too big"); return;
    }

    /* large cdata will be handled here, for example whole large structs */
}

template<typename T>
static inline void *write_int(lua_State *L, int index, int cv, void *stor) {
    lua_Integer v = lua_tointeger(L, index);
    if (use_ffi_signed<T>(cv)) {
        using U = typename std::make_signed<T>::type;
        *static_cast<U *>(stor) = U(v);
    } else {
        using U = typename std::make_unsigned<T>::type;
        *static_cast<U *>(stor) = U(v);
    }
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
                    return write_int<char>(L, index, tp.cv(), &stor->c);
                case ast::C_BUILTIN_SHORT:
                    return write_int<short>(L, index, tp.cv(), &stor->s);
                case ast::C_BUILTIN_INT:
                    return write_int<int>(L, index, tp.cv(), &stor->i);
                case ast::C_BUILTIN_LONG:
                    return write_int<long>(L, index, tp.cv(), &stor->l);
                case ast::C_BUILTIN_LLONG:
                    return write_int<long long>(L, index, tp.cv(), &stor->ll);
                case ast::C_BUILTIN_INT8:
                    return write_int<int8_t>(L, index, tp.cv(), &stor->i8);
                case ast::C_BUILTIN_INT16:
                    return write_int<int16_t>(L, index, tp.cv(), &stor->i16);
                case ast::C_BUILTIN_INT32:
                    return write_int<int32_t>(L, index, tp.cv(), &stor->i32);
                case ast::C_BUILTIN_INT64:
                    return write_int<int64_t>(L, index, tp.cv(), &stor->i64);
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
            return &(stor->ptr = lua_touserdata(L, index));
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
