#include <cstdlib>

#include "parser.hh"
#include "ast.hh"
#include "lib.hh"
#include "lua.hh"
#include "ffi.hh"

static int cffi_cdef(lua_State *L) {
    parser::parse(luaL_checkstring(L, 1));
    return 0;
}

static int cffi_new(lua_State *L) {
    return 0;
}

static int cffi_string(lua_State *L) {
    return 0;
}

static const luaL_Reg cffi_lib[] = {
    /* core */
    {"cdef", cffi_cdef},

    /* data handling */
    {"new", cffi_new},

    /* utilities */
    {"string", cffi_string},

    {NULL, NULL}
};

static int cffi_func_call(lua_State *L) {
    ffi::call_cif(*lua::touserdata<ffi::cdata<ffi::fdata>>(L, 1), L);
    return 1;
}

static void cffi_setup_func_handle_meta(lua_State *L) {
    lua_pushcfunction(L, cffi_func_call);
    lua_setfield(L, -2, "__call");
}

static void cffi_setup_data_handle_meta(lua_State *L) {
}

static int cffi_handle_index(lua_State *L) {
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

    auto *fud = lua::newuserdata<ffi::cdata<ffi::fdata>>(
        L, sizeof(void *[nargs * 2])
    );
    luaL_setmetatable(L, "cffi_func_handle");

    fud->decl = fdecl;
    fud->val.sym = funp;

    if (!ffi::prepare_cif(*fud)) {
        luaL_error(L, "unexpected failure setting up '%s'", func.name.c_str());
    }

    return 1;
}

static int cffi_free_handle(lua_State *L) {
    auto *c_ud = lua::touserdata<lib::handle>(L, 1);
    lib::close(*c_ud);
    return 0;
}

static void cffi_setup_lib_handle_meta(lua_State *L) {
    lua_pushcfunction(L, cffi_free_handle);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, cffi_handle_index);
    lua_setfield(L, -2, "__index");
}

extern "C" int luaopen_cffi(lua_State *L) {
    luaL_newlib(L, cffi_lib);

    auto *c_ud = lua::newuserdata<lib::handle>(L);
    *c_ud = lib::open();
    if (luaL_newmetatable(L, "cffi_lib_handle")) {
        cffi_setup_lib_handle_meta(L);
    }
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "C");

    if (luaL_newmetatable(L, "cffi_func_handle")) {
        cffi_setup_func_handle_meta(L);
    }
    lua_pop(L, 1);

    if (luaL_newmetatable(L, "cffi_data_handle")) {
        cffi_setup_data_handle_meta(L);
    }
    lua_pop(L, 1);

    return 1;
}
