# cffi-lua

[![Build Status](https://github.com/q66/cffi-lua/actions/workflows/build.yaml/badge.svg)](https://github.com/q66/cffi-lua/actions)

This is a portable C FFI for Lua, based on `libffi` and aiming to be mostly
compatible with LuaJIT FFI, but written from scratch. Compatibility is
preserved where reasonable, but not where not easily implementable (e.g.
the parser extensions for 64-bit `cdata` and so on). Thanks to `libffi`,
it works on many operating systems and CPU architectures. The `cffi-lua`
codebase itself does not contain any non-portable code (with the exception
of things such as Windows calling convention handling on x86, and some
adjustments for big endian architectures). Some effort was also taken to
ensure compatibility with custom Lua configurations (e.g. with changed
numeric type representations), though this is not tested or guaranteed
to work (patches welcome if broken).

Unlike LuaJIT's `ffi` module or other efforts such as `luaffifb`, it works
with every common version of the reference Lua implementation (currently 5.1,
5.2, 5.3 and 5.4, 5.0 could be supported but wasn't considered worth it) as
well as compatible non-reference ones (like LuaJIT). Functionality from newer
Lua versions is also supported, when used with that version (e.g. with 5.3+
you will get seamless integer and bit-op support, with 5.4 you will get
metatype support for to-be-closed variables, and so on).

Since it's written from scratch, having 1:1 bug-for-bug C parser compatibility
is a non-goal. The parser is meant to comply with C11, plus a number of
extensions from GCC, MSVC and C++ (where it doesn't conflict with C).

The project was started because there isn't any FFI for standard Lua that's
as user friendly as LuaJIT's and doesn't have portability issues.

## Current status

See `STATUS.md`.

## Notable differences from LuaJIT

- Equality comparisons against `nil` always result in `false`
- Equality comparisons between `cdata` and Lua values are always `false`
- Passing unions (or structs containing unions) is not supported on all platforms
- Bitfields are not supported
- Several new API extensions

Equality comparions work this way due to limitations of the Lua metamethod
semantics. Use `cffi.nullptr` instead. The other limitations are caused by
`libffi` not supporting these features portably.

## Dependencies

The dependencies are kept intentionally minimal.

- A C++ compiler supporting the right subset of C++14
- Lua 5.1 or newer (tested up to and including 5.4) or equivalent (e.g. LuaJIT)
- `libffi`
- `meson`

Optional dependencies:

- `pkg-config` (for automated Lua finding)
- A Lua executable (only for tests)

These toolchains have been tested:

- GCC 7+ (all platforms)
- Clang 8+ (all platforms)
- Visual Studio 2017+ (with updates)

Other toolchains may also work. The theoretical minimum is GCC 4.8 and
Clang 3.4 (an updated VS 2017 is already the minimum, older versions are
missing necessary language features). It is ensured that no non-standard
extensions are used, so as long as your compiler is C++14 compliant, it
should work (technically there are some GCC/Clang/MSVC-specific diagnostic
pragmas used, but these are conditional and only used to control warnings).

The module should work on any CPU architecture supported by `libffi`. The CI
system tests a large variety of CPU architectures (see `STATUS.md`). If you
encounter any issues on yours, please send patches or at least report them
so they can be fixed.

The `pkg-config` tool is optional when using `-Dlua_version=custom` and
`-Dlibffi=custom` (or `vendor`). However, for `custom`, you will need to
manually specify what to include and link.

## Building

On Unix-like systems:

```
$ mkdir build
$ cd build
$ meson ..
$ ninja all
```

This will configure the module for the default Lua version in your system.
If your system does not provide a default `lua.pc` `pkg-config` file, you
will need to explicitly set the version with e.g. `-Dlua_version=5.2`
passed to `meson`. You will also need to do this if you with to compile
for a different Lua version than your default `lua.pc` provides.

By default, a Lua loadable module will be built. This module can be installed
in a path that Lua expects. On Unix-like systems, the `ninja install` target
can do that.

There is also an option to build a static library, by passing `-Dstatic=true`
to `meson`. This is mainly intended for either application usages that will
embed the FFI, or for various specialized platforms that do not support
shared libraries or don't have a version of Lua configured to support modules.
This version is usually not meant to be distributed. To use the static version,
you will need to declare the `luaopen_cffi` symbol with the usual Lua function
signature, `lua_pushcfunction` it on the stack and for example store it in
`package.preload`.

You can also pass `luajit` to `-Dlua_version` to build against LuaJIT (it
will use `luajit.pc` then). Additionally, if you have a different Lua
implementation than that but it still provides the same compliant API,
you can bypass the check with `-Dlua_version=custom` and then provide
the appropriate include path and linkage via `CXXFLAGS` and `LDFLAGS`.

It is also possible to pass `-Dlua_version=vendor`, in which case the
library will be taken from `deps` and the includes from `deps/include`.
The `deps` directory can be either in the source root or in the directory
you run `meson` from.

Keep in mind that on Unix-likes, it is not necessary to actually link against
the Lua library. Even when using `pkg-config`, the build system will always
remove the linkage. The Lua symbols are instead supplied to the module
through the executable it's loaded from. This does not work on Windows,
where you actually need to link against the DLL for Lua modules to work.

When using `homebrew` on macOS, its `libffi` is not installed globally.
Therefore, you will need to set your `PKG_CONFIG_PATH` so that `pkg-config`
can find its `.pc` file.

You can also use `-Dlibffi=custom` if you have a system that does not provide
a `.pc` file for `libffi`. In that case you will need to provide the right
include path in `CXXFLAGS` so that either `<ffi.h>` or `<ffi/ffi.h>` can be
included, plus linkage in `LDFLAGS`.

It is also possible to pass `-Dlua_install_path=...` to override where the
Lua module will be installed. See below for that.

The `shared_libffi` option will make libffi provide `dllimport`-decorated APIs
on Windows; for Lua this is the default as there is always a DLL. On other
systems, it does nothing. This is not strictly necessary, but it will make
things faster when you're really using dynamic versions of those, and it's not
possible to autodetect. Usually, you should be using static libffi on Windows.

### Windows and MSVC style environment

To build on Windows with an MSVC-style toolchain, first get yourself a binary
distribution of `libffi` and the right version of Lua. They must be compatible
with the runtime you're targeting.

Drop the `.lib` files (import lib for Lua, static lib for `libffi`, unless you
link `libffi` dynamically) of `libffi` and `lua` in the `deps` directory (either
in the source root or the directory you are running `meson` from). The naming
is up to you, `meson` will accept library names with or without `lib` prefix,
and the build system accepts both unversioned and versioned to cover all
environments. Usually, for Lua you will have something like `lua53.lib`.
Also, drop the include files for `libffi` (`ffi.h` and `ffitarget.h`) into
`deps/include`, same with the Lua include files.

It is recommended that you always use a static library for `libffi`.

Drop any `.dll` files in the `deps` directory also. This would be the Lua
dll file typically (e.g. `lua53.dll`).

If you wish to run tests, also drop in the Lua executable, following the
same naming scheme as the `.dll` file (or simply called `lua.exe`). If
you don't do that, you will need to pass `-Dtests=false` to `meson` as well.

Afterwards, run `meson` from the `build` directory (create it), like this:

```
meson .. -Dlua_version=vendor -Dlibffi=vendor
```

Then proceed with the usual:

```
ninja all
ninja test
```

Examples of such environment are the Visual Studio environment itself and
also Clang for Windows by default.

### Windows and MinGW/MSYS style environment

This environment is Unix-like, so install the necessary dependencies as you
would on Linux. In an MSYS2 environment, this would be something like:

```
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-pkg-config
pacman -S mingw-w64-x86_64-meson
pacman -S mingw-w64-x86_64-libffi mingw-w64-x86_64-lua
```

Particularly for MSYS2, you should use dependencies from just one repo,
as e.g. `meson` installed from the MSYS2 repo won't detect `mingw-w64`
libraries and so on.

After that, proceed as you would on Linux, except use `shared_libffi`
appropriately. By default, libffi is shared in the MSYS2 environment.

```
meson .. -Dlua_version=5.3 -Dshared_libffi=true
```

You might also want to provide `-static-libgcc -static-libstdc++` in `LDFLAGS`
if you wish to distribute the resulting module/library, otherwise they will
carry dependencies the `libgcc` and `libstdc++-6` DLLs.

Compile and test with:

```
ninja all
ninja test
```

If you have just a plain MinGW compiler and no package manager environment
with it, you will need to set it up manually and use `vendor` or `custom`
for `lua_version` and `libffi`. Keep in mind that you don't need import
libraries for the MinGW compiler, you can link against DLLs directly.

## Installing

```
$ ninja install
```

This will install either the module or the static library depending on how
you have configured the build.

By default, the Lua module will install in `$(libdir)/lua/$(luaver)`, e.g.
`/usr/lib/lua/5.2`. This is the default for most Lua installations. You can
override that with `-Dlua_install_path=...`. The path is the entire
installation path. You can insert `@0@` in it, which will be replaced with
the Lua version you're building for (e.g. `5.2`). No other substitutions are
performed.

The goal of this is to make sure the module will be installed in a location
contained in your Lua's `package.cpath`.

## Testing

The module uses a native Lua executable to run tests. Since by default tests
are enabled, the build system will search for the executable. If your copy of
Lua is in a non-standard path, you can use `-Dlua_path=...` when configuring
to explicitly specify where the executable is stored.

Tests are only runnable when all of the following is met:

- You are not cross-compiling
- You are doing a module build (i.e. not `-Dstatic=true`)
- The Lua executable matches the language version you are building for

Either way, you can run tests with the following:

```
$ ninja test
```

You can see the available test cases in `tests`, they also serve as examples.

Some of the tests only work if `cffi` is built with working `cffi.load`. This
is nearly always true when you are building a module, since `cffi` supports
more targets than Lua itself with module loading.

You can also run the individual test cases standalone, like this:

```
$ lua path/to/cffi/tests/runner.lua path/to/test/case.lua
```

The environment variable `TESTS_PATH` can be used to manually specify the tests
directory. Usually this is not necessary as it's automatically figured out.

You can also specify `CFFI_PATH` as the path where `cffi.so` or `.dll` is stored.
By default, it is assumed default `package.cpath` contains it somewhere.

Additionally, `TESTLIB_PATH` should be specified as a path to the test support
library. This library contains utilities used by some of the tests, like various
native calls and global symbols. By default, it is stored in `build/tests`.
If you do not specify this, tests requiring it will not run.

Test cases ordinarily do not print anything to standard output or error. If the
return code is `0`, the test has succeeded. If it is `77`, the test was skipped,
e.g. because of the testlib not being found. In case of hard failures, an
assertion error will be raised.
