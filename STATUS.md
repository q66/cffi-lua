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

## Passing unions by value

Since `libffi` does not have reliable support for `union`s, `cffi` does not
support it for all targets, only those that are explicitly handled. Currently
this includes:

- 32-bit x86 (all)
- 64-bit x86 (Windows)
- 32-bit ARM (all)
- 32-bit PowerPC
- MIPS(64) (all)

Every other target forbids this. Both passing and returning is forbidden, and
structs containing unions are likewise not allowed. Keep in mind that this
only applies to parameter passing and returning by value; you can pass them by
pointer everywhere, as well as allocate them.

The system will not let you declare such functions on unsupported targets.

The `cffi.abi("unionval")` can be used to check the support from Lua in a generic
manner.

## Missing features

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
- Passing `union` by value is not supported everywhere
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
implementation and a C++14 compiler. We have a continuous integration
setup that tests multiple architectures on Linux as well as `x86_64`
macOS and Windows.

In addition to supporting every Lua version starting with 5.1, effort is
made when it comes to supporting unusual Lua configurations (such as ones
with differently configured numeric types). This is however not tested, so
if you run into issues, feel free to provide a patch.

The CI matrix currently includes the following:

| Architecture | Word size | Endianness | OS      | Compiler | Build type       |
|--------------|-----------|------------|---------|----------|------------------|
| `x86_64`     | 64        | little     | Linux   | `gcc`    | `debugoptimized` |
| `x86_64`     | 64        | little     | Linux   | `gcc`    | `release`        |
| `x86_64`     | 64        | little     | Linux   | `clang`  | `debugoptimized` |
| `x86_64`     | 64        | little     | Windows | `gcc`    | `release`        |
| `x86_64`     | 64        | little     | Windows | `cl.exe` | `debugoptimized` |
| `x86_64`     | 64        | little     | MacOS   | `clang`  | `debugoptimized` |
| `ppc64le`    | 64        | little     | Linux   | `gcc`    | `debugoptimized` |
| `ppc64`      | 64        | big        | Linux   | `gcc`    | `debugoptimized` |
| `aarch64`    | 64        | little     | Linux   | `gcc`    | `debugoptimized` |
| `riscv64`    | 64        | little     | Linux   | `gcc`    | `debugoptimized` |
| `s390x`      | 64        | big        | Linux   | `gcc`    | `debugoptimized` |
| `i686`       | 32        | little     | Linux   | `gcc`    | `debugoptimized` |
| `mips`       | 32        | big        | Linux   | `gcc`    | `debugoptimized` |
| `ppc`        | 32        | big        | Linux   | `gcc`    | `debugoptimized` |
| `armv6l`     | 32        | little     | Linux   | `gcc`    | `debugoptimized` |
| `m68k`       | 32        | big        | Linux   | `gcc`    | `debug`          |

Note: `ppc64` is ABIv1.

This is not an exhaustive list of supported targets but rather just a limited
list intended to cover everything (64-bit systems, 32-bit systems, little and
big endian, Linux, macOS and Windows, and release build to catch assertion
related bugs).
