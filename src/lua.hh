#ifndef LUA_HH
#define LUA_HH

#include <cassert>
#include <cstddef>

#include "platform.hh"
#include "util.hh"

#if defined(FFI_DIAGNOSTIC_PRAGMA_CLANG)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wold-style-cast"
#elif defined(FFI_DIAGNOSTIC_PRAGMA_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#endif

/* force lua API to be marked with dllimport */
#if defined(FFI_WINDOWS_ABI)
#define LUA_BUILD_AS_DLL
#endif

#include <lua.hpp>

#if defined(FFI_DIAGNOSTIC_PRAGMA_CLANG)
#pragma clang diagnostic pop
#elif defined(FFI_DIAGNOSTIC_PRAGMA_GCC)
#pragma GCC diagnostic pop
#endif

#if LUA_VERSION_NUM < 501

#error This Lua version is not supported.

#elif LUA_VERSION_NUM == 501

/* lua 5.1 compat bits
 *
 * defines are used in case e.g. luajit is used which has these funcs
 */

static inline void luaL_setmetatable52(lua_State *L, char const *tname) {
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);
}

#ifdef luaL_setmetatable
#undef luaL_setmetatable
#endif
#define luaL_setmetatable luaL_setmetatable52

static inline void *luaL_testudata52(lua_State *L, int ud, char const *tname) {
    void *p = lua_touserdata(L, ud);
    if (!p || !lua_getmetatable(L, ud)) {
        return nullptr;
    }
    lua_getfield(L, LUA_REGISTRYINDEX, tname);
    if (lua_rawequal(L, -1, -2)) {
        lua_pop(L, 2);
        return p;
    }
    lua_pop(L, 2);
    return nullptr;
}

#ifdef luaL_testudata
#undef luaL_testudata
#endif
#define luaL_testudata luaL_testudata52

static inline std::size_t lua_rawlen52(lua_State *L, int index) {
    return lua_objlen(L, index);
}

#ifdef lua_rawlen
#undef lua_rawlen
#endif
#define lua_rawlen lua_rawlen52

static inline void luaL_newlib52(lua_State *L, luaL_Reg const l[]) {
    lua_newtable(L);
    luaL_register(L, nullptr, l);
}

#ifdef luaL_newlib
#undef luaL_newlib
#endif
#define luaL_newlib luaL_newlib52

#endif /* LUA_VERSION_NUM == 501 */

#if LUA_VERSION_NUM < 503

#ifdef lua_isinteger
#undef lua_isinteger
#endif
#define lua_isinteger(L, idx) int(0)

#endif /* LUA_VERSION_NUM == 503 */

namespace lua {

static constexpr int CFFI_CTYPE_TAG = -128;
static constexpr char const CFFI_CDATA_MT[] = "cffi_cdata_handle";
static constexpr char const CFFI_LIB_MT[] = "cffi_lib_handle";
static constexpr char const CFFI_DECL_STOR[] = "cffi_decl_stor";
static constexpr char const CFFI_PARSER_STATE[] = "cffi_parser_state";

template<typename T>
static T *touserdata(lua_State *L, int index) {
    return static_cast<T *>(lua_touserdata(L, index));
}

static inline int type_error(lua_State *L, int narg, char const *tname) {
    lua_pushfstring(
        L, "%s expected, got %s", tname, lua_typename(L, lua_type(L, narg))
    );
    luaL_argcheck(L, false, narg, lua_tostring(L, -1));
    return 0;
}

static inline void mark_cdata(lua_State *L) {
    luaL_setmetatable(L, CFFI_CDATA_MT);
}

static inline void mark_lib(lua_State *L) {
    luaL_setmetatable(L, CFFI_LIB_MT);
}

#if LUA_VERSION_NUM < 503
/* 5.2 and older uses a simpler (unexposed) alignment */
union user_align_t { void *p; double d; long l; };
#elif LUA_VERSION_NUM < 504
/* 5.3 does not expose this, so mirror its guts */
union user_align_t { lua_Number n; lua_Integer i; void *p; double d; long l; };
#else
/* 5.4+ has the configured alignment in luaconf */
union user_align_t { LUAI_MAXALIGN; };
#endif

} /* namespace lua */

#define LUA_BUG_MSG(L, msg) \
    lua_pushfstring(L, "%s:%s: bug: %s", __FILE__, __LINE__, msg)

#endif /* LUA_HH */
