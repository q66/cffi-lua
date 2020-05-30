/* This is cffi-lua: https://github.com/q66/cffi-lua
 *
 * Copyright (c) 2020 Daniel "q66" Kolesa and contributors
 *
 * The project is provided under the terms of the MIT license, which should
 * be shipped with source and binary distributions of the project.
 *
 * Brief documentation:
 *
 * This file provides the public API to the library.
 * This matches the Lua module API and provides exactly one symbol. When using
 * cffi-lua as a shared module, you can load it from Lua directly and only need
 * the module .so file.
 *
 * However, if you intend to ship it as a part of a runtime or something else
 * where the presence of the module needs to be guaranteed, you will want to
 * use this public interface.
 *
 * Typical usage would be something like:
 *
 * #include <lua.h>
 * #include <cffi-lua.h> // must be included after Lua
 *
 * lua_getglobal(L, "package");
 * lua_getfield(L, -1, "preload");
 * lua_pushcfunction(L, luaopen_cffi);
 * lua_setfield(L, -2, "cffi");
 * lua_pop(L, 2);
 *
 * This will also work with Lua builds that don't have module support compiled.
 *
 * If you are building on Windows and know that you'll be linking against a
 * DLL version of the library, you can define CFFI_LUA_DLL before including
 * the header. If you are unsure, don't define anything.
 */

#ifndef CFFI_LUA_H
#define CFFI_LUA_H

#include "cffi-lua-config.h"

#if !defined(LUA_VERSION_NUM)
#  error "Lua not included"
#endif
#if LUA_VERSION_NUM != CFFI_LUA_LUA_VERSION
#  error "Incorrect Lua version"
#endif

#if defined(__CYGWIN__) || (defined(_WIN32) && !defined(_XBOX_VER))
#  ifdef CFFI_LUA_DLL
#    ifdef CFFI_LUA_BUILD
#      define CFFI_LUA_EXPORT __declspec(dllexport)
#    else
#      define CFFI_LUA_EXPORT __declspec(dllimport)
#    endif
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

#ifdef __cplusplus
extern "C" {
#endif

CFFI_LUA_EXPORT int luaopen_cffi(lua_State *L);

#ifdef __cplusplus
}
#endif

#endif
