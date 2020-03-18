# Detailed status

This file will attempt to document every single feature that the FFI is
supposed to and either has or doesn't have.

If you feel like something is missing here, report it.

## Parsing

C language syntax support.

### Declarations

- [x] Simple functions (`ret func(args);`; note: `ret func();` has no args like C++)
  - [x] Unnamed parameters (C++ extension)
- [x] Variadic functions
- [x] Extern variable declarations
- [x] `typedef`
- [x] Full `struct` (named or unnamed)
- [x] Opaque `struct`
- [x] Opaque `enum` (extension)
- [x] Transparent `struct` inside `struct` (extension)
- [ ] Transparent `enum` inside `struct` (extension)
- [x] `enum`
- [x] `union`
  - [x] Transparent `union` inside `struct`
  - [x] Transaprent `struct` inside `union`
- [ ] `static const` declarations inside `struct`/`union` (C++ extension)

#### Wishlist

- [ ] Struct bitfields

### Types

- [x] Builtin types:
  - [x] `signed`, `unsigned` types (implied `int`)
  - [x] `bool`
  - [x] `_Bool`
  - [x] `char`
  - [x] `short`
  - [x] `int`
  - [x] `long`
  - [x] `long long`
  - [x] `int8_t`
  - [x] `int16_t`
  - [x] `int32_t`
  - [x] `int64_t`
  - [x] `uint8_t`
  - [x] `uint16_t`
  - [x] `uint32_t`
  - [x] `uint64_t`
  - [x] `size_t`
  - [x] `ssize_t`
  - [x] `intptr_t`
  - [x] `uintptr_t`
  - [x] `ptrdiff_t`
  - [x] `wchar_t`
  - [x] `char16_t`
  - [x] `char32_t`
  - [x] `time_t`
  - [x] `float`
  - [x] `double`
  - [x] `long double`
  - [x] `va_list`
  - [x] `__builtin_va_list`
  - [x] `__gnuc_va_list`
  - [ ] `complex`
  - [ ] `_Complex`
  - [ ] `complex double`
  - [ ] `complex float`
- [x] Type qualifiers:
  - [x] `signed`, `unsigned`
  - [x] `const`
  - [x] `__const__`, `__volatile__` (GCC extension)
  - [x] `volatile`
- [x] Pointers (`T *`)
- [x] Function pointers (`T (*)()`)
- [x] C++ references (`T &`) (C++ extension)
- [x] Name lookups
- [x] `struct foo`, `enum foo`
- [x] `struct`/`union` inside types
- [x] Arrays
  - [x] References/pointers to array
- [x] Variable length arrays (C99)
- [x] Flexible array members (C99)
- [ ] `alignas`, `_Alignas`

#### Wishlist

These are implemented by LuaJIT, but not considered urgent.

- [ ] vector types (GCC extension)
- [ ] `__attribute__` (GCC extension)
  - [ ] `packed`
  - [ ] `vector_size`

### Operators

- [x] Arithmetic (`+`, `-`, `*`, `/`, `%`)
- [x] Comparisons (`==`, `!=`, `>`, `<`, `>=`, `<=`)
- [x] AND/OR
- [x] Bitwise opeators (`&`, `|`, `^`, `<<`, `>>`)
- [x] Unary operators (`-`, `+`, `!`, `~`)
- [x] Ternary operator
- [x] Operator precedences
- [x] `alignof`
- [x] `sizeof`
- [x] `__alignof__` (GCC extension)

### Assorted extensions

- [x] Zero-sized arrays, structs, unions (GCC extension)
- [ ] `__extension__` (GCC extension)
- [ ] `__asm__("symbol")` (symbol redirection, GCC extension)
- [ ] `__attribute__` (GCC extension)
  - [ ] `aligned`
  - [ ] `mode`
  - [ ] `cdecl`
  - [ ] `fastcall`
  - [ ] `stdcall`
  - [ ] `thiscall`
- [ ] `__declspec(align(n))` (MSVC)
- [ ] `__cdecl`, `__fastcall`, `__stdcall`, `__thiscall` (MSVC)

#### Wishlist

These are implemented by LuaJIT, but not considered urgent.

- [ ] `__ptr32`, `__ptr64` (MSVC)
- [ ] `#pragma pack` (MSVC)

## Backend

Implements the functionality provided by the parser.

- [x] Function calls
- [x] Variadic function calls
- [x] Variable reads and assignments
- [x] Integers up to 32 bits, `float`, `double`
- [x] `long double`
- [x] 64-bit integers
- [x] `time_t`
- [ ] Complex types
- [x] Pointers
- [x] C++ references
- [x] Enums (except large enums beyond `int`)
- [x] Type conversions
- [x] Typedefs
- [x] Structs
- [x] Unions
- [x] Arrays
- [ ] Alternate calling conventions
- [ ] Constant expressions
  - [x] Simple integer expressions
  - [x] Correct integer promotions
  - [ ] Non-integer type support
- [x] `cdata` arithmetic
- [x] correct `tostring` on integer `cdata`

### Wishlist

These are implemented by LuaJIT, but not considered urgent.

Unions by value are not easily implementable with current `libffi`, as the
feature depends on specifics of the ABI (various types may be passsed in
different registers), and `libffi` has no builtin functionality to deal
with unions.

- [ ] Vectors
- [ ] Unions as arguments/returns by value
- [ ] Struct bitfields

## API

The APIs the FFI should provide.

### Symbols

- [x] `cffi.cdef` (symbol definition)
  - [x] Parameterized types
- [x] `cffi.C` (global namespace)
- [x] `cffi.load` (library namespaces)

### cdata manipulation

- [x] `cffi.new` (object allocation)
- [x] `ctype` constructors (object allocation)
- [x] `cffi.typeof` (`ctype` creation)
  - [x] Parameterized types
- [x] `cffi.cast` (`cdata` typecasts)
- [x] `cffi.metatype` (custom `ctype` metatables)
- [x] `cffi.gc` (custom `cdata` finalizers)
- [x] `cffi.addressof` (custom extension, like `&`: `T` or `T &` -> `T *`)
- [x] `cffi.ref` (custom extension: `T &` -> `T &`, `T` -> `T &`)

### ctype manipulation

- [x] `cffi.sizeof`
- [x] `cffi.alignof`
- [x] `cffi.offsetof` (besides bitfields)
- [x] `cffi.istype` (type checking interface)

### Utilities

- [x] `cffi.errno` (portable `errno` handling)
- [x] `cffi.string` (pointer/array to Lua string)
- [x] `cffi.copy` (`memcpy`)
- [x] `cffi.fill` (`memset`)
- [x] `cffi.tonumber` (`cdata`-aware `tonumber`)
- [x] `cffi.toretval` (custom extension: cdata -> lua return value)
- [x] `cffi.type` (`cdata`-aware `type`)
- [x] `cffi.eval` (custom extension: constant expression -> cdata)
- [x] `cffi.nullptr` (custom extension: a `NULL` pointer constant for cmp)

### Target information

- [x] `cffi.abi` (like luajit + elfv2 for ppc64)
- [x] `cffi.os` (x86, arm, ppc, mips including 64-bit and bi-endian variants)
- [x] `cffi.arch`

### Callbacks

Variadic callbacks are not (and likely will not be) supported.

- [x] Minimal functionality
- [x] `cb:free`
- [x] `cb:set`

## Portability

Windows support is currently missing in the code, but otherwise the other
platforms below have been tested. Architectures currently tested include
`ppc64le`, `ppc64` and `x86_64`. Others should also work, as long as
supported by `libffi`.

- [x] Linux
- [x] FreeBSD (other BSDs may also work; patches welcome if not)
- [ ] Windows
- [x] macOS (dependencies from homebrew; `libffi` needs `PKG_CONFIG_PATH`)
- [x] Little endian
- [x] Big endian

## Build system

Build system enhancements TODO.

- [x] Lua version option
- [ ] Test suite

## Code

Various things to do around the existing code.

- [x] `dlopen` etc. abstraction
- [ ] Type system cleanup
- [ ] Testing infrastructure
- [ ] ... TBD
