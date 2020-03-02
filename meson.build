# Project definition

project('cffi-lua', ['cpp'],
    version: '0.0.1',
    default_options: ['buildtype=plain', 'cpp_std=c++14'],
    meson_version: '>=0.46'
)

# Default compiler warnings at least for gcc/clang

cxx = meson.get_compiler('cpp')

tgt_compiler_id = cxx.get_id()
if tgt_compiler_id == 'gcc' or tgt_compiler_id == 'clang'
    extra_cxxflags = ['-Wall', '-Wextra', '-Wshadow', '-Wold-style-cast']
else
    extra_cxxflags = []
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

cffi_src = [
    'src/main.cc',
    'src/parser.cc',
    'src/ast.cc',
    'src/lib.cc',
    'src/ffi.cc'
]

# follow what lua does, i.e. .so everywhere except windows
plugin_suffix = 'so'

if host_machine.system() == 'windows'
    plugin_suffix = 'dll'
endif

cffi_lib = shared_module('cffi',
    cffi_src, dependencies: [ffi_dep, lua_dep],
    cpp_args: extra_cxxflags, name_prefix: '',
    name_suffix: plugin_suffix
)
