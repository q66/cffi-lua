# cffi-lua

This is a portable C FFI for Lua, based on libffi and loosely modelled after
the LuaJIT FFI. It aims to be compatible with all current Lua versions and
work on a variety of OSes and architectures.

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
