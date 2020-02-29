#ifndef LUA_HH
#define LUA_HH

#include <lua.hpp>

#if LUA_VERSION_NUM < 501

#error This Lua version is not supported.

#elif LUA_VERSION_NUM == 501

/* lua 5.1 compat bits */

static inline void luaL_setmetatable(lua_State *L, char const *tname) {
    luaL_getmetatable(L, tname);
    lua_setmetatable(L, -2);
}

static inline void *luaL_testudata(lua_State *L, int ud, char const *tname) {
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

static inline size_t lua_rawlen(lua_State *L, int index) {
    return lua_objlen(L, index);
}

static inline void luaL_newlib(lua_State *L, luaL_Reg const l[]) {
    lua_newtable(L);
    luaL_register(L, nullptr, l);
}

#endif /* LUA_VERSION_NUM == 501 */

namespace lua {

template<typename T>
static T *newuserdata(lua_State *L, size_t extra = 0) {
    return static_cast<T *>(lua_newuserdata(L, sizeof(T) + extra));
}

template<typename T>
static T *touserdata(lua_State *L, int index) {
    return static_cast<T *>(lua_touserdata(L, index));
}

} /* namespace lua */

#endif /* LUA_HH */
