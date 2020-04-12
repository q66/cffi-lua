# Test suite definitions

runner = executable('runner', 'runner.cc',
    include_directories: [current_inc, runner_inc] + extra_inc,
    dependencies: [lua_dep],
    export_dynamic: true
)

test_cases = [
    # test_name                      test_file                    expected_fail
    ['abi example',                  'abi',                             false],
    ['simple example',               'simple_puts',                     false],
    ['variadic calls',               'variadic',                        false],
    ['structs, arrays, unions',      'struct_array',                    false],
    ['ffi.copy and fill',            'copy_fill',                       false],
    ['callbacks',                    'callbacks',                       false],
    ['table initializers',           'table_init',                      false],
    ['parameterized types',          'parameterized',                   false],
    ['scalar types',                 'scalar',                          false],
    ['symbol redirection',           'redir',                           false],
]

# We put the deps path in PATH because that's where our Lua dll file is
# located and we need the test runner to be able to find this on Windows

penv = environment()
penv.append('PATH', deps_path)

foreach tcase: test_cases
    test(tcase[0], runner,
        args: [
            meson.build_root(),
            join_paths(meson.current_source_dir(), tcase[1] + '.lua')
        ],
        should_fail: tcase[2], depends: cffi_mod,
        env: penv
    )
endforeach
