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

        auto *fdecl = ast::lookup_decl(fname);
        if (!fdecl) {
            luaL_error(L, "missing declaration for symbol '%s'", fname);
        }

        auto &func = fdecl->as<ast::c_function>();
        size_t nargs = func.params().size();

        auto funp = lib::get_func(dl, fname);
        if (!funp) {
            luaL_error(L, "undefined symbol: %s", fname);
        }

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
        luaL_setmetatable(L, "cffi_func_handle");

        fud->decl = fdecl;
        fud->val.sym = funp;

        if (!ffi::prepare_cif(*fud)) {
            luaL_error(
                L, "unexpected failure setting up '%s'", func.name.c_str()
            );
        }

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

/* this is the metatable for function cdata */
struct func_meta {
    static int call(lua_State *L) {
        ffi::call_cif(*lua::touserdata<ffi::cdata<ffi::fdata>>(L, 1), L);
        return 1;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_func_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }
        lua_pushcfunction(L, call);
        lua_setfield(L, -2, "__call");
        lua_pop(L, 1);
    }
};

/* this is the metatable for generic cdata */
struct data_meta {
    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_data_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }
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
        return 0;
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

        auto *c_ud = lua::newuserdata<lib::handle>(L);
        *c_ud = lib::open();

        lib_meta::setup(L);
        func_meta::setup(L);
        data_meta::setup(L);
    }
};

extern "C" int luaopen_cffi(lua_State *L) {
    ffi_module::open(L);
    return 1;
}
