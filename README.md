# cffi-lua

[![Build Status](https://travis-ci.com/q66/cffi-lua.svg?branch=master)](https://travis-ci.com/q66/cffi-lua)

This is a portable C FFI for Lua, based on `libffi` and aiming to be mostly
compatible with LuaJIT FFI, but written from scratch. Compatibility is
preserved where reasonable, but not where not easily implementable (e.g.
the parser extensions for 64-bit `cdata` and so on). It's also meant to be
compatible with all current Lua versions and work on every OS/architecture
combo that `libffi` supports.

Since it's written from scratch, having 1:1 bug-for-bug C parser compatibility
is a non-goal. The parser is meant to comply with C11, plus a number of
extensions from GCC, MSVC and C++ (where it doesn't conflict with C).

The project was started because there isn't any FFI for standard Lua that's
as user friendly as LuaJIT's and doesn't have portability issues.

## Current status

**The project is currently in alpha state.** The core featureset is done,
however some things are missing, and the codebase is not optimal.

It is also not battle tested and I would advise against using it in production
for now.

See `STATUS.md` for a detailed/exhaustive listing.

## Notable differences from LuaJIT

- Equality comparisons against `nil` always result in `false`
- Equality comparisons between `cdata` and Lua values are always `false`
- Passing `union`s (or `struct`s containing `union`s) by value is not supported
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
- `meson`, `pkg-config` (optional)

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
system tests `x86_64`, `ppc64le`, `aarch64` and `s390x`, in addition to local
testing on other architectures. If you encounter any issues on yours, feel
free to provide patches or at least report issues.

The `pkg-config` tool is optional when using `-Dlua_version=custom` and
`-Dlibffi=custom`. However, you will need to manually specify what to include
and link using compiler flags.

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

Three components will be built: a module, a shared library and a static
library. The module does not depend on the libraries, it can be used
alone. The libraries exist for the purpose of linking your program or
library directly against them, so you can embed Lua with an FFI inside
without worrying about Lua having to load the library. You can choose
either dynamic or static version for that.

If you wish to use the library version, it is called `libcffi-lua-VER.so`
on Unix-like systems (`dylib` on macOS) and `cffi-lua-VER.so` on Windows,
where `VER` is the Lua version (e.g. `5.2`). It has a stable API and ABI
with just one exported symbol, `luaopen_cffi`, which matches the standard
Lua module ABI. You can call it from your code and it will leave the
module table on the Lua stack.

You can also pass `luajit` to `-Dlua_version` to build against LuaJIT (it
will use `luajit.pc` then). Additionally, if you have a different Lua
implementation than that but it still provides the same compliant API,
you can bypass the check with `-Dlua_version=custom` and then provide
the appropriate include path and linkage via `CXXFLAGS` and `LDFLAGS`.

It is also possible to pass `-Dlua_version=vendor`, in which case the
library will be taken from `deps` and the includes from `deps/include`.
The `deps` directory can be either in the source root or in the directory
you run `meson` from. On Unix-like systems, the library will be named
`liblua.a`.

When using `homebrew` on macOS, its `libffi` is not installed globally.
Therefore, you will need to set your `PKG_CONFIG_PATH` so that `pkg-config`
can find its `.pc` file.

You can also use `-Dlibffi=custom` if you have a system that does not provide
a `.pc` file for `libffi`. In that case you will need to provide the right
include path in `CXXFLAGS` so that either `<ffi.h>` or `<ffi/ffi.h>` can be
included, plus linkage in `LDFLAGS`.

It is also possible to pass `-Dlua_install_path=...` to override where the
Lua module will be installed. See below for that.

The `shared_lua` and `shared_libffi` options will make Lua and libffi provide
`dllimport`-decorated APIs on Windows. On other systems, they do nothing. This
is not strictly necessary, but it will make things faster when you're really
using dynamic versions of those, and it's not possible to autodetect. Usually,
you should be using dynamic Lua but static libffi on Windows.

### Windows and MSVC style environment

To build on Windows with an MSVC-style toolchain, first get yourself a binary
distribution of `libffi` and the right version of Lua. They must be compatible
with the runtime you're targeting.

Drop the `.lib` files (import libs or static libs) of `libffi` and `lua`
in the `deps` directory (either in the source root or the directory you
are running `meson` from), naming them `libffi.lib` and `liblua.lib`. Drop
the include files for `libffi` (`ffi.h` and `ffitarget.h`) into `deps/include`,
same with the Lua include files.

It is recommended that you use a static library for `libffi`.

Drop any `.dll` files in the `deps` directory also. This would be the Lua
dll file typically (e.g. `lua53.dll`). This is necessary in order to run
tests.

Afterwards, run `meson` from the `build` directory (create it), like this:

```
meson .. -Dlua_version=vendor -Dlibffi=vendor -Dshared_lua=true
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

After that, proceed as you would on Linux, except use `shared_lua` and
`shared_libffi` appropriately. By default, both are shared in the MSYS2
environment.

```
meson .. -Dlua_version=5.3 -Dshared_lua=true -Dshared_libffi=true
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
for `lua_version` and `libffi`.

## Installing

```
$ ninja install
```

This will install the following components:

- The Lua module
- The shared library
- The static library
- A `pkg-config` `.pc` file for the library

See the section above for what these libraries mean.

By default, the Lua module will install in `$(libdir)/lua/$(luaver)`, e.g.
`/usr/lib/lua/5.2`. This is the default for most Lua installations. You can
override that with `-Dlua_install_path=...`. The path is the entire
installation path. You can insert `@0@` in it, which will be replaced with
the Lua version you're building for (e.g. `5.2`). No other substitutions are
performed.

The goal of this is to make sure the module will be installed in a location
contained in your Lua's `package.cpath`.

The `pkg-config` file is called `cffi-lua-VER.pc`, with `VER` being the Lua
version (e.g. `5.2`).

## Testing

```
$ ninja test
```

You can see the available test cases in `tests`, they also serve as examples.
