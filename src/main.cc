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

    static int string_f(lua_State *L) {
        if (!luaL_testudata(L, 1, "cffi_cdata_handle")) {
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
            {"typeof", typeof_f},

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

        /* ctype handles */
        ctype_meta::setup(L);
    }
};

extern "C" int luaopen_cffi(lua_State *L) {
    ffi_module::open(L);
    return 1;
}
