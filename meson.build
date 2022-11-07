# Project definition

project('cffi-lua', ['cpp'],
    version: '0.2.2',
    default_options: [
        'buildtype=debugoptimized', 'b_ndebug=if-release', 'cpp_std=c++14',
        'warning_level=3', 'cpp_rtti=false', 'cpp_eh=none'
    ],
    meson_version: '>=0.50'
)

# Extra compiler warnings for gcc/clang

cxx = meson.get_compiler('cpp')

extra_cxxflags = []

if get_option('buildtype') != 'plain'
    if cxx.has_argument('-Wshadow')
        extra_cxxflags += '-Wshadow'
    endif
    if cxx.has_argument('-Wold-style-cast')
        extra_cxxflags += '-Wold-style-cast'
    endif
endif

# Endianness specification is mandatory

if host_machine.endian() == 'big'
    extra_cxxflags += '-DFFI_BIG_ENDIAN'
else
    extra_cxxflags += '-DFFI_LITTLE_ENDIAN'
endif

# Vendor library path; used to find libs and also added to PATH for Windows

deps_path = get_option('deps_dir')
deps_libs = [
    join_paths(meson.source_root(), deps_path),
    join_paths(meson.build_root(), deps_path)
]
extra_inc = []

# Lua dependency checks

luaver = get_option('lua_version')

if luaver == 'luajit'
    lua_dep = dependency('luajit')
elif luaver != 'auto' and luaver != 'custom' and luaver != 'vendor'
    lua_dep = dependency('lua' + luaver, required: false)
    if not lua_dep.found()
        lua_dep = dependency('lua-' + luaver, required: false)
    endif
    if not lua_dep.found()
        lua_dep = dependency('lua' + ''.join(luaver.split('.')), required: false)
    endif
    if not lua_dep.found()
        lua_dep = dependency('lua')
    endif
    if not lua_dep.version().startswith(luaver)
        error('required lua version not found (got @0@)'
            .format(lua_dep.version()))
    endif
elif luaver == 'custom'
    lua_dep = dependency('', required: false)
elif luaver == 'vendor'
    lua_dep = dependency('', required: false)
    extra_inc += include_directories(join_paths(deps_path, 'include'))
else
    lua_dep = dependency('lua')
endif

# Libffi dependency checks

ffiver = get_option('libffi')

if ffiver == 'custom'
    ffi_dep = dependency('', required: false)
elif ffiver == 'vendor'
    ffi_dep = cxx.find_library('ffi', dirs: deps_libs)
    extra_inc += include_directories(join_paths(deps_path, 'include'))
else
    ffi_dep = dependency('libffi')
endif

# Needed on Linux

dl_lib = cxx.find_library('dl', required: false)

# These are Windows only

if get_option('shared_libffi')
    extra_cxxflags += '-DHAVE_LIBFFI_DLLIMPORT'
endif

# Module build definition

luaver_maj = '5'
luaver_num = cxx.compute_int(
    'LUA_VERSION_NUM', prefix: '#include <lua.hpp>',
    dependencies: lua_dep, include_directories: extra_inc
)
luaver_min = luaver_num - 500
luaver_str = '@0@.@1@'.format(luaver_maj, luaver_min)

if luaver_min < 1
    error('Lua 5.1 or newer is required')
endif

# follow what lua does, i.e. .so everywhere except windows
plugin_suffix = 'so'

if host_machine.system() == 'windows'
    plugin_suffix = 'dll'
endif

cffi_src = [
    'src/util.cc',
    'src/ffilib.cc',
    'src/parser.cc',
    'src/ast.cc',
    'src/lib.cc',
    'src/ffi.cc',
    'src/main.cc'
]

# on windows, we need to link to the dll, the dll has a specific name that
# follows the lua version we depend on; on unix-likes we on the other hand
# do not need the library at all, so skip it
if host_machine.system() == 'windows'
    # msys2, etc
    lua_adep = cxx.find_library('lua', dirs: deps_libs, required: false)
    if not lua_adep.found()
        # lua 5.1 uses lua5.1.dll/lib
        lua_adep = cxx.find_library(
            'lua@0@.@1@'.format(luaver_maj, luaver_min),
            dirs: deps_libs, required: false
        )
    endif
    if not lua_adep.found()
        # lua 5.2 onwards uses lua5.2.dll/lib
        lua_adep = cxx.find_library(
            'lua@0@@1@'.format(luaver_maj, luaver_min), dirs: deps_libs
        )
    endif
else
    lua_adep = lua_dep.partial_dependency(compile_args: true, includes: true)
endif

cffi_deps = [dl_lib, ffi_dep, lua_adep]

if get_option('static')
    cffi = static_library('cffi-lua-@0@'.format(luaver_str),
        cffi_src,
        install: true,
        pic: true,
        dependencies: cffi_deps,
        include_directories: extra_inc,
        cpp_args: extra_cxxflags,
        gnu_symbol_visibility: 'hidden'
    )

    cffi_dep = declare_dependency(
        link_with: cffi,
        include_directories: extra_inc
    )
else
    lua_modpath = get_option('lua_install_path')
    if lua_modpath == 'auto'
        lua_modpath = join_paths(get_option('libdir'), 'lua', '@0@')
    endif

    cffi = shared_module('cffi',
        cffi_src,
        install: true,
        install_dir: lua_modpath.format(luaver_str),
        name_prefix: '',
        name_suffix: plugin_suffix,
        dependencies: cffi_deps,
        cpp_args: ['-DCFFI_LUA_DLL'] + extra_cxxflags,
        include_directories: extra_inc,
        gnu_symbol_visibility: 'hidden'
    )
endif

# Tests

if meson.is_cross_build() and get_option('tests')
    build_tests = get_option('tests_cross')
else
    build_tests = get_option('tests')
endif

if build_tests and not get_option('static')
    # get lua path for the runner
    lua_pathopt = get_option('lua_path')
    if lua_pathopt == 'auto' and luaver == 'vendor'
        lua_exe = find_program(
            join_paths(deps_path, 'lua@0@'.format(luaver_str)),
            join_paths(deps_path, 'lua@0@@1@'.format(luaver_maj, luaver_min)),
            join_paths(deps_path, 'lua'),
            required: true
        )
    elif lua_pathopt == 'auto'
        lua_exe = find_program(
            'lua@0@'.format(luaver_str),
            'lua@0@@1@'.format(luaver_maj, luaver_min),
            'lua',
            required: true
        )
    else
        lua_exe = find_program(lua_pathopt, required: true)
    endif

    # check the lua version matches the library version
    # also checks if it's actually runnable (cross-compiling?)
    ret = run_command(lua_exe, [
        '-e',
        'io.write(_VERSION:match("5.+"))'
    ])
    if ret.stdout() != luaver_str
        error('Lua executable does not match version (@0@ vs @1@)'.format(
            ret.stdout(), luaver_str
        ))
    endif

    subdir('tests')
endif
