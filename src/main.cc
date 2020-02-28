#include <cstdlib>
#include <cstring>
#include <cerrno>

#include "platform.hh"
#include "parser.hh"
#include "ast.hh"
#include "lib.hh"
#include "lua.hh"
#include "ffi.hh"

/* sets up the metatable for library, i.e. the individual namespaces
 * of loaded shared libraries as well as the primary C namespace.
 */
struct lib_meta {
    static int gc(lua_State *L) {
        auto *c_ud = lua::touserdata<lib::handle>(L, 1);
        lib::close(*c_ud);
        return 0;
    }

    static int tostring(lua_State *L) {
        auto dl = lua::touserdata<lib::handle>(L, 1);
        if (*dl == lib::load(nullptr, L)) {
            lua_pushfstring(L, "library: default");
        } else {
            lua_pushfstring(L, "library: %p", static_cast<void *>(*dl));
        }
        return 1;
    }

    static int index(lua_State *L) {
        auto dl = *lua::touserdata<lib::handle>(L, 1);
        char const *sname = luaL_checkstring(L, 2);
        ffi::make_cdata(L, dl, ast::lookup_decl(sname), sname);
        return 1;
    }

    static int newindex(lua_State *L) {
        auto dl = *lua::touserdata<lib::handle>(L, 1);
        char const *sname = luaL_checkstring(L, 2);

        auto const *decl = ast::lookup_decl(sname);
        if (!decl) {
            luaL_error(
                L, "missing declaration for symbol '%s'", decl->name.c_str()
            );
            return 0;
        }
        if (decl->obj_type() != ast::c_object_type::VARIABLE) {
            luaL_error(L, "symbol '%s' is not mutable", decl->name.c_str());
        }

        void *symp = lib::get_var(dl, sname);
        if (!symp) {
            luaL_error(L, "undefined symbol: %s", sname);
            return 0;
        }

        /* FIXME: find a nicer way to get the data written...
         * the union cast is moderately unsafe but should work as long
         * as the typechecking is right, and if it's not, oh well
         */
        ffi::lua_check_cdata(
            L, decl->as<ast::c_variable>().type(),
            static_cast<ast::c_value *>(symp), 3
        );
        return 0;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_lib_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, newindex);
        lua_setfield(L, -2, "__newindex");

        lua_pushcfunction(L, tostring);
        lua_setfield(L, -2, "__tostring");

        lua_setmetatable(L, -2);
        lua_setfield(L, -2, "C");
    }
};

/* used by all kinds of cdata
 *
 * there are several kinds of cdata:
 * - callable cdata (functions)
 * - indexable cdata (pointers, arrays)
 * - value cdata (primitives)
 */
struct cdata_meta {
    static int gc(lua_State *L) {
        auto &fud = *lua::touserdata<ffi::cdata<void *>>(L, 1);
        using T = ast::c_type;
        fud.decl.~T();
        return 0;
    }

    static int tostring(lua_State *L) {
        auto &fud = *lua::touserdata<ffi::cdata<void *>>(L, 1);
        auto s = fud.decl.serialize();
        lua_pushfstring(L, "cdata<%s>: %p", s.c_str(), fud.val);
        return 1;
    }

    static int call(lua_State *L) {
        auto *decl = lua::touserdata<ast::c_type>(L, 1);
        switch (decl->type()) {
            case ast::C_BUILTIN_FPTR:
            case ast::C_BUILTIN_FUNC:
                break;
            default: {
                auto s = decl->serialize();
                luaL_error(L, "'%s' is not callable", s.c_str());
                break;
            }
        }
        return ffi::call_cif(
            *lua::touserdata<ffi::cdata<ffi::fdata>>(L, 1), L
        );
    }

    template<typename F>
    static void index_common(lua_State *L, F &&func) {
        auto *cd = lua::touserdata<ffi::cdata<void **>>(L, 1);
        /* TODO: add arrays */
        switch (cd->decl.type()) {
            case ast::C_BUILTIN_PTR:
                break;
            default: {
                auto s = cd->decl.serialize();
                luaL_error(L, "'%s' is not indexable", s.c_str());
                break;
            }
        }
        auto sidx = luaL_checkinteger(L, 2);
        luaL_argcheck(L, sidx >= 0, 2, "index is negative");
        auto *ptr = reinterpret_cast<unsigned char *>(cd->val);
        auto *type = cd->decl.ptr_base().libffi_type();
        func(*cd, static_cast<void *>(&ptr[sidx * type->size]));
    }

    static int index(lua_State *L) {
        index_common(L, [L](auto &cd, void *val) {
            ffi::lua_push_cdata(L, cd.decl.ptr_base(), val);
        });
        return 1;
    }

    static int newindex(lua_State *L) {
        index_common(L, [L](auto &cd, void *val) {
            ffi::lua_check_cdata(
                L, cd.decl.ptr_base(), static_cast<ast::c_value *>(val), 3
            );
        });
        return 0;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_cdata_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, call);
        lua_setfield(L, -2, "__call");

        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");

        lua_pushcfunction(L, newindex);
        lua_setfield(L, -2, "__newindex");

        lua_pushcfunction(L, tostring);
        lua_setfield(L, -2, "__tostring");

        lua_pop(L, 1);
    }
};

/* represents lua-side type definition */
struct ctype_meta {
    static int gc(lua_State *L) {
        auto &decl = *lua::touserdata<ast::c_type>(L, 1);
        using T = ast::c_type;
        decl.~T();
        return 0;
    }

    static int tostring(lua_State *L) {
        auto &decl = *lua::touserdata<ast::c_type>(L, 1);
        lua_pushfstring(L, "ctype<%s>", decl.serialize().c_str());
        return 1;
    }

    static int call(lua_State *L) {
        auto &decl = *lua::touserdata<ast::c_type>(L, 1);
        switch (decl.type()) {
            case ast::C_BUILTIN_FUNC:
                luaL_argcheck(
                    L, false, 1, "function types cannot be instantiated"
                );
                break;
            default: {
                break;
            }
        }
        ast::c_value stor{};
        if (lua_gettop(L) >= 2) {
            ffi::lua_check_cdata(L, decl, &stor, 2);
        } else {
            memset(&stor, 0, sizeof(stor));
        }
        ffi::lua_push_cdata(L, decl, &stor);
        return 1;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_ctype_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, call);
        lua_setfield(L, -2, "__call");

        lua_pushcfunction(L, tostring);
        lua_setfield(L, -2, "__tostring");

        lua_pop(L, 1);
    }
};

/* the ffi module itself */
struct ffi_module {
    static int cdef_f(lua_State *L) {
        parser::parse(luaL_checkstring(L, 1));
        return 0;
    }

    static int new_f(lua_State *L) {
        /* stack: <first arg> <other args> */
        int nargs = lua_gettop(L);
        /* first arg: ctype */
        if (luaL_testudata(L, 1, "cffi_ctype_handle")) {
            /* first arg is a ctype, duplicate it so
             * we can call it without messing up args
             */
            lua_pushvalue(L, 1);
        } else {
            /* first arg is assumed to be a string, in that case do
             * like a typeof below to get a ctype on top of the stack
             */
            auto *ud = lua::newuserdata<ast::c_type>(L);
            new (ud) ast::c_type{parser::parse_type(luaL_checkstring(L, 1))};
            luaL_setmetatable(L, "cffi_ctype_handle");
        }
        /* stack: <first arg> <other args> <ctype> */
        lua_insert(L, 2);
        /* stack: <first arg> <ctype> <other args> */
        lua_call(L, nargs - 1, 1);
        /* stack: <first arg> <retval> */
        return 1;
    }

    static int load_f(lua_State *L) {
        char const *path = luaL_checkstring(L, 1);
        bool glob = (lua_gettop(L) >= 2) && lua_toboolean(L, 2);
        auto *c_ud = lua::newuserdata<lib::handle>(L);
        *c_ud = lib::load(path, L, glob);
        return 1;
    }

    static int typeof_f(lua_State *L) {
        auto *ud = lua::newuserdata<ast::c_type>(L);
        if (luaL_testudata(L, 1, "cffi_cdata_handle")) {
            new (ud) ast::c_type{*lua::touserdata<ast::c_type>(L, 1)};
        } else {
            new (ud) ast::c_type{parser::parse_type(luaL_checkstring(L, 1))};
        }
        luaL_setmetatable(L, "cffi_ctype_handle");
        return 1;
    }

    static int errno_f(lua_State *L) {
        int cur = errno;
        if (lua_gettop(L) >= 1) {
            errno = int(luaL_checkinteger(L, 1));
        }
        lua_pushinteger(L, cur);
        return 1;
    }

    static int string_f(lua_State *L) {
        if (!luaL_testudata(L, 1, "cffi_cdata_handle")) {
            lua_pushfstring(
                L, "cannot convert '%s' to 'char const *'",
                luaL_typename(L, 1)
            );
            luaL_argcheck(L, false, 1, lua_tostring(L, -1));
        }
        /* FIXME: check argument type conversions */
        auto &ud = *lua::touserdata<ffi::cdata<char const *>>(L, 1);
        if (lua_gettop(L) <= 1) {
            lua_pushstring(L, ud.val);
        } else {
            lua_pushlstring(L, ud.val, size_t(luaL_checkinteger(L, 2)));
        }
        return 1;
    }

    /* FIXME: type conversions (constness etc.) */
    static void *check_voidptr(lua_State *L, int idx) {
        if (luaL_testudata(L, idx, "cffi_cdata_handle")) {
            auto &ud = *lua::touserdata<ffi::cdata<void *>>(L, idx);
            if (ud.decl.type() != ast::C_BUILTIN_PTR) {
                lua_pushfstring(
                    L, "cannot convert '%s' to 'void *'",
                    ud.decl.serialize().c_str()
                );
                luaL_argcheck(L, false, idx, lua_tostring(L, -1));
            }
            return ud.val;
        } else if (lua_isuserdata(L, idx)) {
            return lua_touserdata(L, idx);
        }
        lua_pushfstring(
            L, "cannot convert '%s' to 'void *'",
            luaL_typename(L, 1)
        );
        luaL_argcheck(L, false, idx, lua_tostring(L, -1));
        return nullptr;
    }

    /* FIXME: lengths (and character) in these APIs may be given by cdata... */

    static int copy_f(lua_State *L) {
        void *dst = check_voidptr(L, 1);
        void const *src;
        size_t len;
        if (lua_isstring(L, 2)) {
            src = lua_tostring(L, 2);
            if (lua_gettop(L) <= 2) {
                len = lua_rawlen(L, 2);
            } else {
                len = size_t(luaL_checkinteger(L, 3));
            }
        } else {
            src = check_voidptr(L, 2);
            len = size_t(luaL_checkinteger(L, 3));
        }
        memcpy(dst, src, len);
        return 0;
    }

    static int fill_f(lua_State *L) {
        void *dst = check_voidptr(L, 1);
        size_t len = size_t(luaL_checkinteger(L, 2));
        int byte = int(luaL_optinteger(L, 3, 0));
        memset(dst, byte, len);
        return 0;
    }

    static int abi_f(lua_State *L) {
        luaL_checkstring(L, 1);
        lua_pushvalue(L, 1);
        lua_rawget(L, lua_upvalueindex(1));
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_pushboolean(L, false);
        }
        return 1;
    }

    static void setup_abi(lua_State *L) {
        lua_newtable(L);
        lua_pushboolean(L, true);
#if FFI_WORDSIZE == 64
        lua_setfield(L, -2, "64bit");
#elif FFI_WORDSIZE == 32
        lua_setfield(L, -2, "32bit");
#elif FFI_WORDSIZE == 16
        lua_setfield(L, -2, "16bit");
#else
        lua_setfield(L, -2, "8bit");
#endif
        lua_pushboolean(L, true);
#ifdef FFI_BIG_ENDIAN
        lua_setfield(L, -2, "be");
#else
        lua_setfield(L, -2, "le");
#endif
#ifdef FFI_WINDOWS_ABI
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "win");
#endif
#ifdef FFI_ARM_EABI
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "eabi");
#endif
#if FFI_ARCH == FFI_ARCH_PPC64 && defined(_CALL_ELF) && _CALL_ELF == 2
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "elfv2");
#endif
#if FFI_ARCH_HAS_FPU == 1
        lua_pushboolean(L, true);
        lua_setfield(L, -2, "fpu");
#endif
        lua_pushboolean(L, true);
#if FFI_ARCH_SOFTFP == 1
        lua_setfield(L, -2, "softfp");
#else
        lua_setfield(L, -2, "hardfp");
#endif
    }

    static void setup(lua_State *L) {
        static const luaL_Reg lib_def[] = {
            /* core */
            {"cdef", cdef_f},
            {"load", load_f},

            /* data handling */
            {"new", new_f},
            {"typeof", typeof_f},

            /* utilities */
            {"errno", errno_f},
            {"string", string_f},
            {"copy", copy_f},
            {"fill", fill_f},

            {NULL, NULL}
        };
        luaL_newlib(L, lib_def);

        lua_pushliteral(L, FFI_OS_NAME);
        lua_setfield(L, -2, "os");

        lua_pushliteral(L, FFI_ARCH_NAME);
        lua_setfield(L, -2, "arch");

        setup_abi(L);
        lua_pushcclosure(L, abi_f, 1);
        lua_setfield(L, -2, "abi");
    }

    static void open(lua_State *L) {
        setup(L); /* push table to stack */

        /* lib handles */
        auto *c_ud = lua::newuserdata<lib::handle>(L);
        *c_ud = lib::load(nullptr, L, false);
        lib_meta::setup(L);

        /* cdata handles */
        cdata_meta::setup(L);

        /* ctype handles */
        ctype_meta::setup(L);
    }
};

extern "C" int luaopen_cffi(lua_State *L) {
    ffi_module::open(L);
    return 1;
}
