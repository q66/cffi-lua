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
        luaL_setmetatable(L, "cffi_cdata_f_handle");

        new (&fud->decl) ast::c_type{
            fdecl->as<ast::c_function>(), 0, ast::C_BUILTIN_FUNC
        };
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

/* used by all kinds of cdata
 *
 * there are several kinds of cdata:
 * - callable cdata (functions)
 * - indexable cdata (pointers, arrays)
 * - value cdata (primitives)
 */
struct cdata_meta {
    static int gc(lua_State *L) {
        auto &fud = *lua::touserdata<ffi::cdata<ffi::fdata>>(L, 1);
        using T = ast::c_type;
        fud.decl.~T();
        return 0;
    }
};

struct cdata_f_meta: cdata_meta {
    static int call(lua_State *L) {
        ffi::call_cif(*lua::touserdata<ffi::cdata<ffi::fdata>>(L, 1), L);
        return 1;
    }

    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_cdata_f_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pushcfunction(L, call);
        lua_setfield(L, -2, "__call");
        lua_pop(L, 1);
    }
};

struct cdata_p_meta: cdata_meta {
    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_cdata_p_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

        lua_pop(L, 1);
    }
};

struct cdata_v_meta: cdata_meta {
    static void setup(lua_State *L) {
        if (!luaL_newmetatable(L, "cffi_cdata_v_handle")) {
            luaL_error(L, "unexpected error: registry reinitialized");
        }

        lua_pushcfunction(L, gc);
        lua_setfield(L, -2, "__gc");

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

        /* lib handles */
        auto *c_ud = lua::newuserdata<lib::handle>(L);
        *c_ud = lib::open();
        lib_meta::setup(L);

        /* cdata handles */
        cdata_f_meta::setup(L);
        cdata_p_meta::setup(L);
        cdata_v_meta::setup(L);
    }
};

extern "C" int luaopen_cffi(lua_State *L) {
    ffi_module::open(L);
    return 1;
}
