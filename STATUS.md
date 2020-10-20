# Current status

Currently, the FFI supports most features one would expect it to support,
notably:

- Near complete C11 declarations syntax, plus the following extensions:
  - Unnamed parameters
  - Opaque `enum` declarations
  - Transparent `struct` inside `struct` or `union`
  - `__const__`, `__volatile__`
  - C++ references
  - `__alignof__`
  - Zero sized arrays, structs and unions
  - `__asm__("symbol")` (redirection)
  - `__cdecl`, `__fastcall`, `__stdcall`, `__thiscall`
  - `__attribute__` with: `cdecl`, `fastcall`, `stdcall`, `thiscall`
  - Empty argument list is treated like C++, i.e. `void foo();` has no args
- All API supported by LuaJIT FFI, plus the following extensions:
  - `cffi.addressof` (like C++ `&`: `T` or `T &` becomes `T *`)
  - `cffi.toretval` (cdata -> Lua return value conversion)
  - `cffi.eval` (constant expression -> cdata)
  - `cffi.nullptr` (a `NULL` pointer constant for comparisons)
  - `cffi.tonumber` (`cdata`-aware `tonumber`)
  - `cffi.type` (`cdata`-aware `type`)
- Semantics generally follow LuaJIT closely, with these exceptions:
  - All metamethods of the respective Lua version are respected
  - Lua integers are supported (and used) when using Lua 5.3 or newer
  - Windows `__stdcall` functions must be explicitly marked as such
  - Equality comparisons of `cdata` and `nil` do not work (use `cffi.nullptr`)
    - This is a limitation of the Lua metamethod semantics
  - You can reassign the values of fields of a `metatype`
    - Which fields are used is decided at `cffi.metatype` call time, though
  - `cffi.gc` can be used with any `cdata`
  - Callbacks are currently unrestricted (no limit, no handle reuse)
    - This may change in the future, so do not rely on it

## TODO

There are some things notably missing at this point.

### Supported by LuaJIT

- Syntax:
  - Transparent `enum` inside `struct` (non-standard extension)
  - `static const` declarations inside `struct`/`union` (C++ extension)
- Bitfields (`libffi` limitation)
- Complex types (`complex`, `_Complex`, `complex double`, `complex float`)
- `alignas` (and `_Alignas`)
- Vector types (GCC extension)
- `__attribute__` with `packed`, `vector_size`, `aligned`, `mode` (GCC extension)
- `__extension__` (GCC extension)
- `__declspec(align(n))` (MSVC extension)
- `__ptr32`, `__ptr64` (MSVC extension)
- `#pragma pack` (MSVC extension)
- Passing `union` as arguments and return values (`libffi` limitation)
- `__stdcall` on Windows is not auto-guessed and must be tagged explicitly

### Not supported by LuaJIT

- Non-integer support in constant expressions
- Builtin C preprocessor

### Internals

- Type system cleanup/rework
- Performance optimizations
- Resource usage optimizations

## Platform support

The project supports every system and architecture that has a `libffi`
implementation and a C++14 compiler. The CI explicitly tests `x86_64`,
`ppc64le`, `aarch64` and `s390x`, on Linux, Windows and macOS operating
systems. Little and big endian architectures are both supported. Currently
32-bit systems lack a CI, as they are not provided by Travis.

In addition to supporting every Lua version starting with 5.1, effort is
made when it comes to supporting unusual Lua configurations (such as ones
with differently configured numeric types). This is however not tested, so
if you run into issues, feel free to provide a patch.
