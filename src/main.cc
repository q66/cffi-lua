/* This file exists as it may be compiled in multiple versions depending on
 * how the entry point is exported. This is particularly true on Windows, where
 * when compiling as DLL, the 'luaopen_cffi' symbol must be dllexport, while
 * for a static lib it must be unmarked.
 *
 * On Unix-like platforms, all symbols are hidden by default if supported by
 * the compiler, with 'luaopen_cffi' being the sole visible symbol, this is
 * not dependent on how we're compiling it.
 */

#include "lua.hh"

#if defined(__CYGWIN__) || (defined(_WIN32) && !defined(_XBOX_VER))
#  ifdef CFFI_LUA_DLL
#    define CFFI_LUA_EXPORT __declspec(dllexport)
#  else
#    define CFFI_LUA_EXPORT
#  endif
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define CFFI_LUA_EXPORT __attribute__((visibility("default")))
#  else
#    define CFFI_LUA_EXPORT
#  endif
#endif

void ffi_module_open(lua_State *L);

extern "C" CFFI_LUA_EXPORT int luaopen_cffi(lua_State *L) {
    ffi_module_open(L);
    return 1;
}
