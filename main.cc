#include <cstdlib>
#include <dlfcn.h>

#include <lua.hpp>

#include <ffi.h>

#include "parser.hh"

static int cffi_cdef(lua_State *L) {
    parser::parse(luaL_checkstring(L, 1));
    return 0;
}

static const luaL_Reg cffi_lib[] = {
    {"cdef", cffi_cdef},

    {NULL, NULL}
};

static int cffi_free_handle(lua_State *L) {
    void **c_ud = static_cast<void **>(lua_touserdata(L, 1));
    dlclose(*c_ud);
    return 0;
}

static void cffi_setup_lib_handle_meta(lua_State *L) {
    lua_pushcfunction(L, cffi_free_handle);
    lua_setfield(L, -2, "__gc");
}

extern "C" int luaopen_cffi(lua_State *L) {
    luaL_newlib(L, cffi_lib);

    void **c_ud = static_cast<void **>(lua_newuserdata(L, sizeof(void *)));
    *c_ud = dlopen(nullptr, RTLD_NOW);
    if (luaL_newmetatable(L, "cffi_lib_handle")) {
        cffi_setup_lib_handle_meta(L);
        lua_setmetatable(L, -2);
    }
    lua_setfield(L, -2, "C");

    return 1;
}
