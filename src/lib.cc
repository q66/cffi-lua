#include "platform.hh"

#include <cstdint>

#ifdef FFI_USE_DLFCN
#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#endif

#include "lib.hh"

#if FFI_OS == FFI_OS_WINDOWS
#include <cstdlib>
#endif

namespace lib {

static int make_cache(lua_State *L) {
    lua_newtable(L);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

#ifdef FFI_USE_DLFCN

#ifdef FFI_OS_CYGWIN
#  define FFI_DL_SOPREFIX "cyg"
#else
#  define FFI_DL_SOPREFIX "lib"
#endif

#if FFI_OS == FFI_OS_OSX
#  define FFI_DL_SONAME "%s.dylib"
#elif defined(FFI_OS_CYGWIN)
#  define FFI_DL_SONAME "%s.dll"
#else
#  define FFI_DL_SONAME "%s.so"
#endif

#if defined(RTLD_DEFAULT)
#  define FFI_DL_DEFAULT RTLD_DEFAULT
#elif FFI_OS == FFI_OS_BSD || FFI_OS == FFI_OS_OSX
#  define FFI_DL_DEFAULT reinterpret_cast<void *>(std::intptr_t(-2))
#else
#  define FFI_DL_DEFAULT nullptr
#endif

/* low level dlfcn handling */

static handle open(char const *path, bool global) {
    return dlopen(path, RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
}

void close(c_lib *cl, lua_State *L) {
    luaL_unref(L, LUA_REGISTRYINDEX, cl->cache);
    cl->cache = LUA_REFNIL;
    if (cl->h != FFI_DL_DEFAULT) {
        dlclose(cl->h);
    }
    cl->h = nullptr;
}

static void *get_sym(c_lib const *cl, char const *name) {
    return dlsym(cl->h, name);
}

/* library resolution */

static char const *resolve_name(lua_State *L, char const *name) {
    if (std::strchr(name, '/')
#ifdef FFI_OS_CYGWIN
        || std::strchr(name, '\\')
#endif
    ) {
        /* input is a path */
        lua_pushstring(L, name);
        return lua_tostring(L, -1);
    }
    if (!std::strchr(name, '.')) {
        /* name without ext */
        lua_pushfstring(L, FFI_DL_SONAME, name);
    } else {
        lua_pushstring(L, name);
#ifdef FFI_OS_CYGWIN
        /* name with ext on cygwin can be used directly (no prefix) */
        return lua_tostring(L, -1);
#endif
    }
    if (!std::strncmp(name, FFI_DL_SOPREFIX, sizeof(FFI_DL_SOPREFIX) - 1)) {
        /* lib/cyg prefix found */
        return lua_tostring(L, -1);
    }
    /* no prefix, so prepend it */
    lua_pushliteral(L, FFI_DL_SOPREFIX);
    lua_insert(L, -2);
    lua_concat(L, 2);
    return lua_tostring(L, -1);
}

/* ldscript handling logic generally adapted from luajit... */

static bool check_ldscript(char const *buf, char const *&beg, char const *&end) {
    char const *p;
    if ((
        !std::strncmp(buf, "GROUP", 5) || !std::strncmp(buf, "INPUT", 5)
    ) && (p = std::strchr(buf, '('))) {
        while (*++p == ' ') {}
        char const *e = p;
        while (*e && (*e != ' ') && (*e != ')')) {
            ++e;
        }
        beg = p;
        end = e;
        return true;
    }
    return false;
}

static bool resolve_ldscript(
    lua_State *L, char const *nbeg, char const *nend
) {
    lua_pushlstring(L, nbeg, nend - nbeg);
    FILE *f = std::fopen(lua_tostring(L, -1), "r");
    lua_pop(L, 1);
    if (!f) {
        return false;
    }
    char buf[256];
    if (!std::fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return false;
    }
    char const *pb, *pe;
    bool got = false;
    if (!std::strncmp(buf, "/* GNU ld script", 16)) {
        while (fgets(buf, sizeof(buf), f)) {
            got = check_ldscript(buf, pb, pe);
            if (got) {
                break;
            }
        }
    } else {
        got = check_ldscript(buf, pb, pe);
    }
    std::fclose(f);
    if (got) {
        lua_pushlstring(L, pb, pe - pb);
    }
    return got;
}

void load(c_lib *cl, char const *path, lua_State *L, bool global) {
    if (!path) {
        /* primary namespace */
        cl->h = FFI_DL_DEFAULT;
        cl->cache = make_cache(L);
        lua::mark_lib(L);
        return;
    }
    handle h = open(resolve_name(L, path), global);
    lua_pop(L, 1);
    if (h) {
        lua::mark_lib(L);
        cl->h = h;
        cl->cache = make_cache(L);
        return;
    }
    char const *err = dlerror(), *e;
    if (
        err && (*err == '/') && (e = std::strchr(err, ':')) &&
        resolve_ldscript(L, err, e)
    ) {
        h = open(lua_tostring(L, -1), global);
        lua_pop(L, 1);
        if (h) {
            lua::mark_lib(L);
            cl->h = h;
            cl->cache = make_cache(L);
            return;
        }
        err = dlerror();
    }
    luaL_error(L, err ? err : "dlopen() failed");
}

bool is_c(c_lib const *cl) {
    return (cl->h == FFI_DL_DEFAULT);
}

#elif FFI_OS == FFI_OS_WINDOWS /* FFI_USE_DLFCN */

/* This is generally adapted from LuaJIT source code.
 *
 * Didn't bother with the UWP bits yet; that may be added later if anybody
 * actually needs that (Lua does not support dynamic modules with UWP though,
 * so only the library version would work)
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 4
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 2
BOOL WINAPI GetModuleHandleExA(DWORD, LPCSTR, HMODULE*);
#endif

#define FFI_DL_DEFAULT reinterpret_cast<void *>(std::intptr_t(-1))

enum {
    FFI_DL_HANDLE_EXE = 0,
    FFI_DL_HANDLE_DLL,
    FFI_DL_HANDLE_CRT,
    FFI_DL_HANDLE_KERNEL32,
    FFI_DL_HANDLE_USER32,
    FFI_DL_HANDLE_GDI32,
    FFI_DL_HANDLE_MAX
};

static void *ffi_dl_handle[FFI_DL_HANDLE_MAX] = {0};

static void dl_error(lua_State *L, char const *fmt, char const *name) {
    auto err = GetLastError();
    wchar_t wbuf[128];
    char buf[256];
    if (!FormatMessageW(
        FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM,
        nullptr, err, 0, wbuf, sizeof(wbuf) / sizeof(wchar_t), nullptr
    ) || !WideCharToMultiByte(
        CP_ACP, 0, wbuf, 128, buf, 256, nullptr, nullptr
    )) {
        buf[0] = '\0';
    }
    luaL_error(L, fmt, name, buf);
}

static bool dl_need_ext(char const *s) {
    while (*s) {
        if ((*s == '/') || (*s == '\\') || (*s == '.')) {
            return false;
        }
        ++s;
    }
    return true;
}

static char const *dl_ext_name(lua_State *L, char const *name) {
    lua_pushstring(L, name);
    if (dl_need_ext(name)) {
        lua_pushliteral(L, ".dll");
        lua_concat(L, 2);
    }
    return lua_tostring(L, -1);
}

void load(c_lib *cl, char const *path, lua_State *L, bool) {
    if (!path) {
        /* primary namespace */
        cl->h = FFI_DL_DEFAULT;
        cl->cache = make_cache(L);
        lua::mark_lib(L);
        return;
    }
    auto olderr = GetLastError();
    handle h = static_cast<handle>(
        LoadLibraryExA(dl_ext_name(L, path), nullptr, 0)
    );
    lua_pop(L, 1);
    if (!h) {
        dl_error(L, "cannot load module '%s': %s", path);
    }
    SetLastError(olderr);
    cl->h = h;
    cl->cache = make_cache(L);
    lua::mark_lib(L);
}

void close(c_lib *cl, lua_State *L) {
    luaL_unref(L, LUA_REGISTRYINDEX, cl->cache);
    cl->cache = LUA_REFNIL;
    if (cl->h == FFI_DL_DEFAULT) {
        for (int i = FFI_DL_HANDLE_KERNEL32; i < FFI_DL_HANDLE_MAX; ++i) {
            void *p = ffi_dl_handle[i];
            if (p) {
                ffi_dl_handle[i] = nullptr;
                FreeLibrary(static_cast<HMODULE>(cl->h));
            }
        }
    } else if (cl->h) {
        FreeLibrary(static_cast<HMODULE>(cl->h));
    }
}

static void *get_sym(c_lib const *cl, char const *name) {
    if (cl->h != FFI_DL_DEFAULT) {
        return util::pun<void *>(
            GetProcAddress(static_cast<HMODULE>(cl->h), name)
        );
    }
    for (std::size_t i = 0; i < FFI_DL_HANDLE_MAX; ++i) {
        if (!ffi_dl_handle[i]) {
            HMODULE h = nullptr;
            char const *dlh = nullptr;
            switch (i) {
                case FFI_DL_HANDLE_EXE:
                    GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, nullptr, &h
                    );
                    break;
                case FFI_DL_HANDLE_CRT: {
                    dlh = util::pun<char const *>(&_fmode);
                    goto handle_dll;
                }
                case FFI_DL_HANDLE_DLL:
                    dlh = util::pun<char const *>(ffi_dl_handle);
                handle_dll:
                    GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        dlh, &h
                    );
                    break;
                case FFI_DL_HANDLE_KERNEL32:
                    h = LoadLibraryExA("kernel32.dll", nullptr, 0);
                    break;
                case FFI_DL_HANDLE_USER32:
                    h = LoadLibraryExA("user32.dll", nullptr, 0);
                    break;
                case FFI_DL_HANDLE_GDI32:
                    h = LoadLibraryExA("gdi32.dll", nullptr, 0);
                    break;
                default:
                    break;
            }
            if (!h) {
                continue;
            }
            ffi_dl_handle[i] = static_cast<void *>(h);
        }
        HMODULE h = static_cast<HMODULE>(ffi_dl_handle[i]);
        auto paddr = GetProcAddress(h, name);
        if (paddr) {
            return util::pun<void *>(paddr);
        }
    }
    return nullptr;
}

bool is_c(c_lib const *cl) {
    return (cl->h == FFI_DL_DEFAULT);
}

#else

void load(c_lib *, char const *, lua_State *L, bool) {
    luaL_error(L, "no support for dynamic library loading on this target");
    return nullptr;
}

void close(c_lib *, lua_State *) {
}

static void *get_sym(c_lib const *, char const *) {
    return nullptr;
}

bool is_c(c_lib const *) {
    return true;
}

#endif /* FFI_USE_DLFCN, FFI_OS == FFI_OS_WINDOWS */

void *get_sym(c_lib const *cl, lua_State *L, char const *name) {
    lua_rawgeti(L, LUA_REGISTRYINDEX, cl->cache);
    lua_getfield(L, -1, name);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        void *p = get_sym(cl, name);
        if (!p) {
            lua_pop(L, 1);
            luaL_error(L, "undefined symbol: %s", name);
            return nullptr;
        }
        lua_pushlightuserdata(L, p);
        lua_setfield(L, -2, name);
        lua_pop(L, 1);
        return p;
    }
    void *p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return p;
}

} /* namespace lib */
