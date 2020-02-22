project('cffi-lua', ['cpp'],
    version: '0.0.1',
    default_options: ['buildtype=plain', 'cpp_std=c++14'],
    meson_version: '>=0.46'
)

tgt_compiler_id = meson.get_compiler('cpp').get_id()
if tgt_compiler_id == 'gcc' or tgt_compiler_id == 'clang'
    extra_cxxflags = ['-Wextra', '-Wshadow', '-Wold-style-cast']
else
    extra_cxxflags = []
endif

ffi_dep = dependency('libffi')
lua_dep = dependency('lua')

cffi_src = [
    'main.cc',
    'parser.cc'
]

cffi_lib = shared_module('cffi',
    cffi_src, dependencies: [ffi_dep, lua_dep],
    cpp_args: extra_cxxflags, name_prefix: ''
)
