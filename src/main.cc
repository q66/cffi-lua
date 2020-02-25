#include <cstdlib>

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

    static int index(lua_State *L) {
        auto dl = *lua::touserdata<lib::handle>(L, 1);
        char const *fname = luaL_checkstring(L, 2);
        ffi::make_cdata(L, dl, ast::lookup_decl(fname), fname);
        return 1;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_lib_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, index);
        lua_setfield(L, -2, "__index");

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
        auto t = decl->type();
        switch (t) {
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

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_cdata_handle")) {
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
        return 0;
    }

    static int string_f(lua_State *L) {
        if (!luaL_checkudata(L, 1, "cffi_cdata_handle")) {
            lua_pushfstring(
                L, "cannot convert '%s' to 'char const *'",
                luaL_typename(L, 1)
            );
            luaL_argcheck(L, false, 1, lua_tostring(L, -1));
        }
        /* FIXME: check argument type conversions */
        auto &fud = *lua::touserdata<ffi::cdata<char const *>>(L, 1);
        lua_pushstring(L, fud.val);
        return 1;
    }

    static void setup(lua_State *L) {
        static const luaL_Reg lib_def[] = {
            /* core */
            {"cdef", cdef_f},

            /* data handling */
            {"new", new_f},

            /* utilities */
            {"string", string_f},

            {NULL, NULL}
        };
        luaL_newlib(L, lib_def);
    }

    static void open(lua_State *L) {
        setup(L); /* push table to stack */

        /* lib handles */
        auto *c_ud = lua::newuserdata<lib::handle>(L);
        *c_ud = lib::open();
        lib_meta::setup(L);

        /* cdata handles */
        cdata_meta::setup(L);
    }
};

extern "C" int luaopen_cffi(lua_State *L) {
    ffi_module::open(L);
    return 1;
}
