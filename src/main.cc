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
#include "platform.hh"

#ifdef FFI_WINDOWS_ABI
#  ifdef FFI_BUILD_DLL
#    define FFI_EXPORT __declspec(dllexport)
#  else
#    define FFI_EXPORT
#  endif
#else
#  if defined(FFI_BUILD_DLL)
/* -Wunused-macros */
#  endif
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define FFI_EXPORT __attribute__((visibility("default")))
#  else
#    define FFI_EXPORT
#  endif
#endif

void ffi_module_open(lua_State *L);

extern "C" FFI_EXPORT int luaopen_cffi(lua_State *L) {
    ffi_module_open(L);
    return 1;
}
