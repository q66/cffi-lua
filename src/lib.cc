/* TODO: implement for windows */

#include "platform.hh"

#ifdef FFI_USE_DLFCN
#include <dlfcn.h>

#include <cstdio>
#include <cstring>
#include <cstdint>

#include <string>
#endif

#include "lib.hh"

namespace lib {

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
#  define FFI_DL_DEFAULT reinterpret_cast<void *>(intptr_t(-2))
#else
#  define FFI_DL_DEFAULT nullptr
#endif

/* low level dlfcn handling */

static handle open(char const *path, bool global) {
    return dlopen(path, RTLD_LAZY | (global ? RTLD_GLOBAL : RTLD_LOCAL));
}

void close(handle h) {
    if (h == FFI_DL_DEFAULT) {
        return;
    }
    dlclose(h);
}

void *get_sym(handle h, char const *name) {
    return dlsym(h, name);
}

/* library resolution */

static char const *resolve_name(lua_State *L, char const *name) {
    if (strchr(name, '/')
#ifdef FFI_OS_CYGWIN
        || strchr(name, '\\')
#endif
    ) {
        /* input is a path */
        lua_pushstring(L, name);
        return lua_tostring(L, -1);
    }
    if (!strchr(name, '.')) {
        /* name without ext */
        lua_pushfstring(L, FFI_DL_SONAME, name);
    } else {
        lua_pushstring(L, name);
#ifdef FFI_OS_CYGWIN
        /* name with ext on cygwin can be used directly (no prefix) */
        return lua_tostring(L, -1);
#endif
    }
    if (!strncmp(name, FFI_DL_SOPREFIX, sizeof(FFI_DL_SOPREFIX) - 1)) {
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

static std::string check_ldscript(char const *buf) {
    char const *p;
    if ((
        !strncmp(buf, "GROUP", 5) || !strncmp(buf, "INPUT", 5)
    ) && (p = strchr(buf, '('))) {
        while (*++p == ' ') {}
        char const *e = p;
        while (*e && (*e != ' ') && (*e != ')')) {
            ++e;
        }
        return std::string{p, e};
    }
    return std::string{};
}

static std::string resolve_ldscript(std::string name) {
    FILE *f = fopen(name.c_str(), "r");
    if (!f) {
        return nullptr;
    }
    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        return nullptr;
    }
    std::string p;
    if (!strncmp(buf, "/* GNU ld script", 16)) {
        while (fgets(buf, sizeof(buf), f)) {
            p = check_ldscript(buf);
            if (!p.empty()) {
                break;
            }
        }
    } else {
        p = check_ldscript(buf);
    }
    fclose(f);
    return p;
}

handle load(char const *path, lua_State *L, bool global) {
    if (!path) {
        /* primary namespace */
        return FFI_DL_DEFAULT;
    }
    handle h = open(resolve_name(L, path), global);
    lua_pop(L, 1);
    if (h) {
        lua::mark_lib(L);
        return h;
    }
    char const *err = dlerror(), *e;
    std::string lds;
    if (err && (*err == '/') && (e = strchr(err, ':')) && !(
        lds = resolve_ldscript(std::string{err, e})
    ).empty()) {
        h = open(lds.c_str(), global);
        if (h) {
            lua::mark_lib(L);
            return h;
        }
        err = dlerror();
    }
    luaL_error(L, err ? err : "dlopen() failed");
    return nullptr;
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

#define FFI_DL_DEFAULT reinterpret_cast<void *>(intptr_t(-1))

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
        nullptr, 0, wbuf, sizeof(wbuf) / sizeof(wchar_t), nullptr
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

static std::string dl_ext_name(char const *name) {
    std::string ret{name};
    if (dl_need_ext(name)) {
        ret += ".dll";
    }
    return ret;
}

handle load(char const *path, lua_State *L, bool) {
    auto olderr = GetLastError();
    handle h = static_cast<handle>(
        LoadLibraryExA(dl_ext_name(name).c_str(), nullptr, 0)
    );
    if (!h) {
        dl_error(L, "cannot load module '%s': %s", path);
    }
    SetLastError(olderr);
    return h;
}

void close(handle h) {
    if (h == FFI_DL_DEFAULT) {
        for (auto i = FFI_DL_HANDLE_KERNEL32; i < FFI_DL_HANDLE_MAX; ++i) {
            void *p = ffi_dl_handle[i];
            if (p) {
                ffi_dl_handle[i] = nullptr;
                FreeLibrary(static_cast<HINSTANCE>(h));
            }
        }
    } else if (h) {
        FreeLibrary(static_cast<HINSTANCE>(h));
    }
}

void *get_sym(handle h, char const *name) {
    if (h != FFI_DL_DEFAULT) {
        return static_cast<void *>(
            GetProcAddress(static_cast<HINSTANCE>(h), name)
        );
    }
    for (size_t i = 0; i < FFI_DL_HANDLE_MAX; ++i) {
        if (!ffi_dl_handle[i]) {
            HINSTANCE h;
            switch (i) {
                case FFI_DL_HANDLE_EXE:
                    GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, NULL, &h
                    );
                    break;
                case FFI_DL_HANDLE_DLL:
                    GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<char const *>(ffi_dl_handle), &h
                    );
                    break;
                case FFI_DL_HANDLE_CRT:
                    GetModuleHandleExA(
                        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        reinterpret_cast<char const *>(&_fmode), &h
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
        HINSTANCE h = static_cast<HINSTANCE>(ffi_dl_handle[i]);
        auto *p = static_cast<void *>(GetProcAddress(h, name));
        if (p) {
            return p;
        }
    }
    return nullptr;
}

#else

handle load(char const *, lua_State *L, bool) {
    luaL_error(L, "no support for dynamic library loading on this OS");
    return nullptr;
}

void close(handle) {
}

void *get_sym(handle, char const *) {
    return nullptr;
}

#endif /* FFI_USE_DLFCN, FFI_OS == FFI_OS_WINDOWS */

} /* namespace lib */
