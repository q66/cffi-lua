# Test suite definitions

testlib = shared_module('testlib', ['testlib.cc'],
    install: false,
    cpp_args: extra_cxxflags
)

test_cases = [
    # test_name                      test_file             expected_fail minver
    ['simple example',               'simple',                    false,  501],
    ['abi example',                  'abi',                       false,  501],
    ['variadic calls',               'variadic',                  false,  501],
    ['structs, arrays, unions',      'struct_array',              false,  501],
    ['struct passing',               'struct_pass',               false,  501],
    ['structs, unions array fields', 'struct_union_array_fields', false,  501],
    ['unions by value',              'unionval',                  false,  501],
    ['global variables',             'globals',                   false,  501],
    ['memory-related utilities',     'copy_fill',                 false,  501],
    ['memory serialization',         'dump_string',               false,  501],
    ['callbacks',                    'callbacks',                 false,  501],
    ['table initializers',           'table_init',                false,  501],
    ['parameterized types',          'parameterized',             false,  501],
    ['scalar types',                 'scalar',                    false,  501],
    ['symbol redirection',           'redir',                     false,  501],
    ['calling conventions',          'callconv',                  false,  501],
    ['constant expressions',         'cexpr',                     false,  501],
    ['redefinitions',                'redef',                     false,  501],
    ['casting rules',                'cast',                      false,  501],
    ['type checks',                  'istype',                    false,  501],
    ['metatype',                     'metatype',                  false,  501],
    ['metatype (5.4)',               'metatype54',                false,  504],
]

# We put the deps path in PATH because that's where our Lua dll file is
# located and we need the Lua executable to be able to find this on Windows

penv = environment()
penv.append('PATH', deps_path)
penv.append('CFFI_PATH', meson.build_root())
penv.append('TESTS_PATH', meson.current_source_dir())
penv.append('TESTLIB_PATH', testlib.full_path())

foreach tcase: test_cases
    if luaver_num < tcase[3]
        continue
    endif
    test(tcase[0], lua_exe,
        args: [
            join_paths(meson.current_source_dir(), 'runner.lua'),
            join_paths(meson.current_source_dir(), tcase[1] + '.lua')
        ],
        should_fail: tcase[2], depends: cffi,
        env: penv
    )
endforeach
