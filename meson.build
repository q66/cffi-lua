# Project definition

project('cffi-lua', ['cpp'],
    version: '0.1.1',
    default_options: [
        'buildtype=debugoptimized', 'cpp_std=c++14', 'warning_level=3',
        'cpp_rtti=false', 'cpp_eh=none'
    ],
    meson_version: '>=0.50'
)

# Extra compiler warnings for gcc/clang + symbol visibility

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

if cxx.has_argument('-fvisibility=hidden')
    extra_cxxflags += '-fvisibility=hidden'
endif

# Configuration data; optional but helps

config_h = configuration_data()

config_h.set('SIZEOF_POINTER', cxx.sizeof('void *'))

if host_machine.endian() == 'big'
    config_h.set10('HAVE_BIG_ENDIAN', true)
endif

if cxx.has_header('endian.h')
    config_h.set10('HAVE_ENDIAN_H', true)
endif

extra_cxxflags += '-DHAVE_CONFIG_H=1'

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
    lua_dep = cxx.find_library('lua', dirs: deps_libs)
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
    config_h.set10('HAVE_FFI_H', true)
else
    ffi_dep = dependency('libffi')
endif

# Needed on Linux

dl_lib = cxx.find_library('dl', required: false)

# Header checks

if ffiver != 'vendor'
    if cxx.has_header('ffi.h', dependencies: ffi_dep)
        config_h.set10('HAVE_FFI_H', true)
    elif cxx.has_header('ffi/ffi.h', dependencies: ffi_dep)
        config_h.set10('HAVE_FFI_FFI_H', true)
    else
        error('libffi header file not found')
    endif
endif

if luaver != 'vendor'
    if not cxx.has_header('lua.hpp', dependencies: lua_dep)
        error('lua header file not found')
    endif
endif

# These are Windows only

if get_option('shared_libffi')
    config_h.set10('HAVE_LIBFFI_DLLIMPORT', true)
endif

# Write configuration data

configure_file(output: 'config.h', configuration: config_h)

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

# avoid linking module against lua except on windows where it's needed
if host_machine.system() == 'windows'
    lua_pdep = lua_dep
else
    lua_pdep = lua_dep.partial_dependency(compile_args: true, includes: true)
endif

cffi_deps = [dl_lib, ffi_dep, lua_pdep]

if get_option('static')
    cffi = static_library('cffi-lua-@0@'.format(luaver_str),
        cffi_src,
        install: true,
        pic: true,
        dependencies: cffi_deps,
        include_directories: extra_inc,
        cpp_args: extra_cxxflags
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
        include_directories: extra_inc
    )
endif

# Tests

runner_inc = include_directories('src')
current_inc = include_directories('.')

if get_option('tests')
    subdir('tests')
endif
