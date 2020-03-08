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

#else /* FFI_USE_DLFCN */
#  error "Not yet implemented"
#endif /* FFI_USE_DLFCN */

} /* namespace lib */
