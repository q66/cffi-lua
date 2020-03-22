# cffi-lua

This is a portable C FFI for Lua, based on `libffi` and aiming to be mostly
compatible with LuaJIT FFI, but written from scratch. Compatibility is
preserved where reasonable, but not where not easily implementable (e.g.
the parser extensions for 64-bit `cdata` and so on). It's also meant to be
compatible with all current Lua versions and work on every OS/architecture
combo that `libffi` supports.

Since it's written from scratch, having 1:1 bug-for-bug C parser compatibility
is a non-goal. The parser is meant to comply with C99, plus a number of
extensions from GCC, MSVC and C++ (where it doesn't conflict with C).

The project was started because there isn't any FFI for standard Lua that's
as user friendly as LuaJIT's and doesn't have portability issues.

## Current status

**The project is currently in alpha state.**. The core featureset is done,
however some things are missing, and the codebase is not optimal.

It is also not battle tested and I would advise against using it in production
for now.

See `STATUS.md` for a detailed/exhaustive listing.

## Dependencies

The dependencies are kept intentionally minimal.

- A Unix-like system (Linux/macOS/some BSD/...); Windows support is TODO
- A C++ compiler supporting the right subset of C++14
- Lua 5.1 or newer (tested up to 5.4) or equivalent (e.g. LuaJIT)
- `libffi`
- `meson`, `pkg-config` (optional)

Any reasonably modern version of GCC or Clang should work fine (GCC 5, maybe
even 4.8, or Clang 3.4 or newer). Other compilers will also work if they
provide the necessary C++ standard compilance; there aren't any compiler
specific extensions used in the code (other than some diagnostic pragmas
to handle warnings under GCC/Clang; these are not enabled elsewhere).

The module should work on any CPU architecture supported by `libffi`. It has
been tested at least on 64-bit PowerPC (little and big endian) and x86.
If you find that the module does not work on yours, report a bug (and maybe
provide a patch).

The `pkg-config` tool is optional when using `-Dlua_version=custom` and
`-Dlibffi=custom`.

## Building

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

You can also pass `luajit` to `-Dlua_version` to build against LuaJIT (it
will use `luajit.pc` then). Additionally, if you have a different Lua
implementation than that but it still provides the same compliant API,
you can bypass the check with `-Dlua_version=custom` and then provide
the appropriate include path and linkage via `CXXFLAGS` and `LDFLAGS`.

When using `homebrew` on macOS, its `libffi` is not installed globally.
Therefore, you will need to set your `PKG_CONFIG_PATH` so that `pkg-config`
can find its `.pc` file.

You can also use `-Dlibffi=custom` if you have a system that does not provide
a `.pc` file for `libffi`. In that case you will need to provide the right
include path in `CXXFLAGS` so that either `<ffi.h>` or `<ffi/ffi.h>` can be
included, plus linkage in `LDFLAGS`.

## Testing

Simply run `ninja test`. You can see the available test cases in `tests`,
they also serve as examples. For actual usage, simply install the built
module in a path specified in your Lua's `package.cpath`.
