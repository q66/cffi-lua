project('cffi-lua', ['cpp'],
    version: '0.0.1',
    default_options: ['buildtype=plain', 'cpp_std=c++14'],
    meson_version: '>=0.46'
)

cxx = meson.get_compiler('cpp')

tgt_compiler_id = cxx.get_id()
if tgt_compiler_id == 'gcc' or tgt_compiler_id == 'clang'
    extra_cxxflags = ['-Wall', '-Wextra', '-Wshadow', '-Wold-style-cast']
else
    extra_cxxflags = []
endif

config_h = configuration_data()

config_h.set('SIZEOF_POINTER', cxx.sizeof('void *'))

if host_machine.endian() == 'big'
    config_h.set10('HAVE_BIG_ENDIAN', true)
endif

if cxx.has_header('endian.h')
    config_h.set10('HAVE_ENDIAN_H', true)
endif

configure_file(output: 'config.h', configuration: config_h)

add_global_arguments('-DHAVE_CONFIG_H=1', language: 'cpp')

ffi_dep = dependency('libffi')

luaver = get_option('lua_version')
if luaver == '5.4'
    lua_dep = dependency('lua5.4', required: false)
    if not lua_dep.found()
        lua_dep = dependency('lua-5.4', required: false)
        if not lua_dep.found()
            lua_dep = dependency('lua54', required: false)
        endif
    endif
elif luaver == '5.3'
    lua_dep = dependency('lua5.3', required: false)
    if not lua_dep.found()
        lua_dep = dependency('lua-5.3', required: false)
        if not lua_dep.found()
            lua_dep = dependency('lua53', required: false)
        endif
    endif
elif luaver == '5.2'
    lua_dep = dependency('lua5.2', required: false)
    if not lua_dep.found()
        lua_dep = dependency('lua-5.2', required: false)
        if not lua_dep.found()
            lua_dep = dependency('lua52', required: false)
        endif
    endif
elif luaver == '5.1'
    lua_dep = dependency('lua5.1', required: false)
    if not lua_dep.found()
        lua_dep = dependency('lua-5.1', required: false)
        if not lua_dep.found()
            lua_dep = dependency('lua51', required: false)
        endif
    endif
else
    lua_dep = dependency('lua')
    luaver = lua_dep.version()
endif

if luaver != 'auto' and not lua_dep.found()
    lua_dep = dependency('lua')
endif

if not lua_dep.version().startswith(luaver)
    error('required lua version not found (got @0@)'.format(lua_dep.version()))
endif

cffi_src = [
    'src/main.cc',
    'src/parser.cc',
    'src/ast.cc',
    'src/lib.cc',
    'src/ffi.cc'
]

cffi_lib = shared_module('cffi',
    cffi_src, dependencies: [ffi_dep, lua_dep],
    cpp_args: extra_cxxflags, name_prefix: ''
)
