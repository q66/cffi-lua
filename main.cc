#include <cstdlib>

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

extern "C" int luaopen_cffi(lua_State *L) {
    luaL_newlib(L, cffi_lib);

    return 1;
}
