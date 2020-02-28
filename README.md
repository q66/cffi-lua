# cffi-lua

This is a portable C FFI for Lua, based on `libffi` and aiming to be mostly
compatible with LuaJIT FFI, but written from scratch. Compatibility is
preserved where reasonable, but not where not easily implementable (e.g.
the parser extensions for 64-bit `cdata` and so on). It's alos meant to be
compatible with all current Lua versions and work on every OS/architecture
combo that `libffi` supports.

Since it's written from scratch, having 1:1 bug-for-bug C parser compatibility
is a non-goal. The parser is meant to comply with C99, plus a number of
extensions from GCC, MSVC and C++ (where it doesn't conflict with C).

The project was started because there isn't any FFI for standard Lua that's
as user friendly as LuaJIT's and doesn't have portability issues.

**This is currently a very rough work in progress and not suitable for production.**

## Current status

See `STATUS.md` for a detailed listing.

## Dependencies

The dependencies are kept intentionally minimal.

- A Unix-like system
- A C++ compiler supporting the right subset of C++14
- Lua 5.2 or newer
- `libffi`
- `meson`, `pkg-config`

## Building

```
$ mkdir build
$ cd build
$ meson ..
$ ninja all
```

## Testing

The `test.lua` file can be run to see what the library can currently do.
Otherwise, you can take `cffi.so` and `require()` it from Lua as usual.
