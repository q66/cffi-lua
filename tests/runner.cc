#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <lua.hh>

#define TEST_STDCALL
#define TEST_FASTCALL

#ifdef FFI_WINDOWS_ABI
#  if FFI_ARCH == FFI_ARCH_X86
#    undef TEST_STDCALL
#    undef TEST_FASTCALL
#    define TEST_STDCALL __stdcall
#    define TEST_FASTCALL __fastcall
#  endif
#  define DLL_EXPORT __declspec(dllexport)
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define DLL_EXPORT __attribute__((visibility("default")))
#  else
#    define DLL_EXPORT
#  endif
#endif

#ifdef CFFI_STATIC
extern "C" int luaopen_cffi(lua_State *L);
#endif

extern "C" DLL_EXPORT
size_t test_strlen(char const *str) {
    return strlen(str);
}

extern "C" DLL_EXPORT
int test_snprintf(char *buf, size_t n, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, n, fmt, args);
    va_end(args);
    return ret;
}

extern "C" DLL_EXPORT
int test_add_ptr(int p[2]) {
    return p[0] + p[1];
}

extern "C" DLL_EXPORT
const char test_string[] = "foobar";

extern "C" DLL_EXPORT
const int test_ints[3] = {42, 43, 44};

extern "C" DLL_EXPORT
int TEST_STDCALL test_stdcall(int a, int b) {
    return a + b;
}

extern "C" DLL_EXPORT
int TEST_FASTCALL test_fastcall(int a, int b) {
    return a + b;
}

struct test_struct {
    int a, b;
};

extern "C" DLL_EXPORT
test_struct test_struct_val(test_struct a, test_struct b) {
    return test_struct{a.a + b.a, a.b + b.b};
}

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
#ifdef CFFI_STATIC
    lua_getfield(L, -1, "preload");
    lua_pushcfunction(L, luaopen_cffi);
    lua_setfield(L, -2, "cffi");
    lua_pop(L, 1);
#else
    lua_pushstring(L, argv[1]);
    lua_pushstring(L, LUA_DIRSEP);
#ifdef FFI_WINDOWS_ABI
    lua_pushstring(L, "?.dll");
#else
    lua_pushstring(L, "?.so");
#endif
    lua_concat(L, 3);
    lua_setfield(L, -2, "cpath");
#endif
    lua_pop(L, 1);

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
