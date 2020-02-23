#include <cstdlib>
#include <dlfcn.h>

#include <lua.hpp>

#include <ffi.h>

#include "parser.hh"
#include "state.hh"

static int cffi_cdef(lua_State *L) {
    parser::parse(luaL_checkstring(L, 1));
    return 0;
}

static const luaL_Reg cffi_lib[] = {
    {"cdef", cffi_cdef},

    {NULL, NULL}
};

/* FIXME: proof-of-concept for now, will not unwind stack, etc */

struct cffi_cdata {
    void (*sym)();
    parser::c_object const *decl;
};

static int cffi_func_call(lua_State *L) {
    cffi_cdata *fud = static_cast<cffi_cdata *>(lua_touserdata(L, 1));

    ffi_cif cif;
    ffi_arg rval;

    auto &func = *static_cast<parser::c_function const *>(fud->decl);
    auto &pdecls = func.params();
    auto &rdecl = func.result();

    std::vector<void *> argsv;
    argsv.reserve(pdecls.size() * 3);

    ffi_type **args = reinterpret_cast<ffi_type **>(&argsv[0]);
    void **valps = &argsv[pdecls.size()];
    void **vals = &argsv[pdecls.size() * 2];

    if (ffi_prep_cif(
        &cif, FFI_DEFAULT_ABI, pdecls.size(), &ffi_type_sint, args
    ) != FFI_OK) {
        luaL_error(L, "unexpected failure calling '%s'", func.name.c_str());
    }

    for (size_t i = 0; i < pdecls.size(); ++i) {
        args[i] = &ffi_type_pointer;
        /* 1 is the userdata */
        char const **s = reinterpret_cast<char const **>(
            const_cast<void const **>(&valps[i])
        );
        *s = luaL_checkstring(L, i + 2);
        vals[i] = s;
    }

    ffi_call(&cif, fud->sym, &rval, vals);
    lua_pushinteger(L, int(rval));
    return 1;
}

static void cffi_setup_func_handle_meta(lua_State *L) {
    lua_pushcfunction(L, cffi_func_call);
    lua_setfield(L, -2, "__call");
}

static int cffi_handle_index(lua_State *L) {
    void *dl = *static_cast<void **>(lua_touserdata(L, 1));
    char const *fname = luaL_checkstring(L, 2);

    auto *fdecl = state::lookup_decl(fname);
    if (!fdecl) {
        luaL_error(L, "missing declaration for symbol '%s'", fname);
    }

    void *funp = dlsym(dl, fname);
    if (!funp) {
        luaL_error(L, "undefined symbol: %s", fname);
    }

    auto *fud = static_cast<cffi_cdata *>(
        lua_newuserdata(L, sizeof(cffi_cdata))
    );
    luaL_setmetatable(L, "cffi_func_handle");

    fud->sym = reinterpret_cast<void (*)()>(funp);
    fud->decl = fdecl;

    return 1;
}

static int cffi_free_handle(lua_State *L) {
    void **c_ud = static_cast<void **>(lua_touserdata(L, 1));
    dlclose(*c_ud);
    return 0;
}

static void cffi_setup_lib_handle_meta(lua_State *L) {
    lua_pushcfunction(L, cffi_free_handle);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, cffi_handle_index);
    lua_setfield(L, -2, "__index");
}

extern "C" int luaopen_cffi(lua_State *L) {
    luaL_newlib(L, cffi_lib);

    void **c_ud = static_cast<void **>(lua_newuserdata(L, sizeof(void *)));
    *c_ud = dlopen(nullptr, RTLD_NOW);
    if (luaL_newmetatable(L, "cffi_lib_handle")) {
        cffi_setup_lib_handle_meta(L);
    }
    lua_setmetatable(L, -2);
    lua_setfield(L, -2, "C");

    if (luaL_newmetatable(L, "cffi_func_handle")) {
        cffi_setup_func_handle_meta(L);
    }
    lua_pop(L, 1);

    return 1;
}
