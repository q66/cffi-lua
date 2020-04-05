#include <cstdio>
#include <cstdlib>

#include <lua.hh>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("not enough arguments (%d)\n", argc);
        return 1;
    }
    /* set up a lua state */
    auto L = luaL_newstate();
    luaL_openlibs(L);
    /* we need a controlled environment */
    lua_getglobal(L, "package");
    lua_pushstring(L, argv[1]);
    lua_pushstring(L, LUA_DIRSEP);
#ifdef FFI_WINDOWS_ABI
    lua_pushstring(L, "?.dll");
#else
    lua_pushstring(L, "?.so");
#endif
    lua_concat(L, 3);
    lua_setfield(L, -2, "cpath");

    /* this will be useful */
    lua_pushcfunction(L, [](lua_State *LL) -> int {
        lua_close(LL);
        exit(77);
    });
    lua_setglobal(L, "skip_test");

    /* load test case */
    if (luaL_loadfile(L, argv[2]) != 0) {
        printf("failed loading file '%s': %s\n", argv[2], lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    lua_call(L, 0, 0);
    lua_close(L);
    return 0;
}
