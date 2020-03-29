# Project definition

project('cffi-lua', ['cpp'],
    version: '0.0.3',
    default_options: [
        'buildtype=debugoptimized', 'cpp_std=c++14', 'warning_level=3'
    ],
    meson_version: '>=0.46'
)

# Extra compiler warnings for gcc/clang + symbol visibility

cxx = meson.get_compiler('cpp')

if get_option('buildtype') != 'plain'
    if cxx.has_argument('-Wshadow')
        add_global_arguments('-Wshadow', language: 'cpp')
    endif
    if cxx.has_argument('-Wold-style-cast')
        add_global_arguments('-Wold-style-cast', language: 'cpp')
    endif
endif

if cxx.has_argument('-fvisibility=hidden')
    add_global_arguments('-fvisibility=hidden', language: 'cpp')
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

add_global_arguments('-DHAVE_CONFIG_H=1', language: 'cpp')

# Lua dependency checks

luaver = get_option('lua_version')

if luaver == 'luajit'
    lua_dep = dependency('luajit')
elif luaver != 'auto' and luaver != 'custom'
    lua_dep = dependency('lua' + luaver, required: false)
    if not lua_dep.found()
        lua_dep = dependency('lua-' + luaver, required: false)
    endif
    if not lua_dep.found()
        lua_dep = dependency(''.join(luaver.split('.')), required: false)
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
else
    lua_dep = dependency('lua')
endif

# Libffi dependency checks

ffiver = get_option('libffi')

if ffiver == 'custom'
    ffi_dep = dependency('', required: false)
else
    ffi_dep = dependency('libffi')
endif

# Needed on Linux

dl_lib = cxx.find_library('dl', required : false)

# Header checks

if cxx.has_header('ffi.h', dependencies: ffi_dep)
    config_h.set10('HAVE_FFI_H', true)
elif cxx.has_header('ffi/ffi.h', dependencies: ffi_dep)
    config_h.set10('HAVE_FFI_FFI_H', true)
else
    error('libffi header file not found')
endif

if not cxx.has_header('lua.hpp', dependencies: lua_dep)
    error('lua header file not found')
endif

# Write configuration data

configure_file(output: 'config.h', configuration: config_h)

# Module build definition

luaver_maj = '5'
luaver_min = cxx.compute_int(
    'LUA_VERSION_NUM - 500', prefix: '#include <lua.hpp>',
    dependencies: lua_dep
)
luaver_str = '@0@.@1@'.format(luaver_maj, luaver_min)

# follow what lua does, i.e. .so everywhere except windows
plugin_suffix = 'so'

if host_machine.system() == 'windows'
    plugin_suffix = 'dll'
endif

cffi_src = [
    'src/ffilib.cc',
    'src/parser.cc',
    'src/ast.cc',
    'src/lib.cc',
    'src/ffi.cc'
]

cffi_deps = [dl_lib, ffi_dep, lua_dep]

cffi_core = static_library('cffi-core', cffi_src, pic: true)

cffi_lib_s = static_library('cffi-lua-@0@'.format(luaver_str),
    ['src/main.cc'],
    install: true,
    pic: true,
    dependencies: cffi_deps,
    link_whole: [cffi_core]
)

# the soname can remain the same forever, the API/ABI will not change
# (it's a single function and its interface is defined for the Lua version)
cffi_lib_d = shared_library('cffi-lua-@0@'.format(luaver_str),
    ['src/main.cc'],
    version: '1.0.0',
    install: true,
    dependencies: cffi_deps,
    link_whole: [cffi_core],
    cpp_args: ['-DFFI_BUILD_DLL']
)

lua_modpath = get_option('lua_install_path')
if lua_modpath == 'auto'
    lua_modpath = join_paths(get_option('libdir'), 'lua', '@0@')
endif

cffi_mod = shared_module('cffi',
    ['src/main.cc'],
    install: true,
    install_dir: lua_modpath.format(luaver_str),
    name_prefix: '',
    name_suffix: plugin_suffix,
    dependencies: cffi_deps,
    link_whole: [cffi_core],
    cpp_args: ['-DFFI_BUILD_DLL']
)

# Pkg-config file for the library

pkgc = import('pkgconfig')
pkgc.generate(cffi_lib_d)

# Tests

runner_inc = include_directories('src')
current_inc = include_directories('.')

subdir('tests')
