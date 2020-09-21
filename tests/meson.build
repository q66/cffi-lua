# Test suite definitions

runner_link = []
runner_cargs = []
if get_option('static')
    runner_link += [cffi]
    runner_cargs += ['-DCFFI_STATIC']
endif

runner = executable('runner', 'runner.cc',
    include_directories: [current_inc, runner_inc] + extra_inc,
    dependencies: [dl_lib, lua_dep],
    link_with: runner_link,
    cpp_args: runner_cargs + extra_cxxflags,
    export_dynamic: true
)

test_cases = [
    # test_name                      test_file            expected_fail  minver
    ['abi example',                  'abi',                      false,   501],
    ['simple example',               'simple_puts',              false,   501],
    ['variadic calls',               'variadic',                 false,   501],
    ['structs, arrays, unions',      'struct_array',             false,   501],
    ['ffi.copy and fill',            'copy_fill',                false,   501],
    ['callbacks',                    'callbacks',                false,   501],
    ['table initializers',           'table_init',               false,   501],
    ['parameterized types',          'parameterized',            false,   501],
    ['scalar types',                 'scalar',                   false,   501],
    ['symbol redirection',           'redir',                    false,   501],
    ['calling conventions',          'callconv',                 false,   501],
    ['constant expressions',         'cexpr',                    false,   501],
    ['redefinitions',                'redef',                    false,   501],
    ['casting rules',                'cast',                     false,   501],
    ['metatype',                     'metatype',                 false,   501],
    ['metatype (5.4)',               'metatype54',               false,   504],
]

# We put the deps path in PATH because that's where our Lua dll file is
# located and we need the test runner to be able to find this on Windows

penv = environment()
penv.append('PATH', deps_path)

foreach tcase: test_cases
    if luaver_num < tcase[3]
        continue
    endif
    test(tcase[0], runner,
        args: [
            meson.build_root(),
            join_paths(meson.current_source_dir(), tcase[1] + '.lua')
        ],
        should_fail: tcase[2], depends: cffi,
        env: penv
    )
endforeach
