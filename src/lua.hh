#ifndef LUA_HH
#define LUA_HH

#include <lua.hpp>

namespace lua {

template<typename T>
static T *newuserdata(lua_State *L, size_t extra = 0) {
    return reinterpret_cast<T *>(lua_newuserdata(L, sizeof(T) + extra));
}

template<typename T>
static T *touserdata(lua_State *L, int index) {
    return static_cast<T *>(lua_touserdata(L, index));
}

} /* namespace lua */

#endif /* LUA_HH */
