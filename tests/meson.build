# Test suite definitions

runner = executable('runner', 'runner.cc', include_directories: [
    current_inc, runner_inc
], dependencies: [lua_dep])

test_cases = [
    # test_name                      test_file                    expected_fail
    ['simple example',               'simple_puts',                     false],
    ['variadic calls',               'variadic',                        false],
    ['callbacks',                    'callbacks',                       false],
    ['table initializers',           'table_init',                      false],
    ['ffi.copy and fill',            'copy_fill',                       false],
    ['structs, arrays, unions',      'struct_array',                    false],
]

foreach tcase: test_cases
    test(tcase[0], runner,
        args: [
            meson.build_root(),
            join_paths(meson.current_source_dir(), tcase[1] + '.lua')
        ],
        should_fail: tcase[2], depends: cffi_lib
    )
endforeach
