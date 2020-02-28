# Detailed status

This file will attempt to document every single feature that the FFI is
supposed to and either has or doesn't have.

If you feel like something is missing here, report it.

## Parsing

C language syntax support.

### Declarations

- [x] Simple functions (`ret func(args);`)
- [ ] Variadic functions
- [x] Extern variable declarations
- [x] `typedef`
- [x] Full `struct` (named or unnamed)
  - [ ] Bitfields
- [ ] Opaque `struct`
- [ ] Opaque `enum` (extension)
- [ ] Transparent `struct`/`union` inside `struct` (extension)
- [ ] Transparent `enum` inside `struct` (extension)
- [x] `enum`
- [ ] `union`
- [ ] `static const` declarations inside `struct`/`union` (C++ extension)

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
  - [ ] `wchar_t`
  - [x] `time_t`
  - [x] `float`
  - [x] `double`
  - [x] `long double`
  - [ ] `va_list`
  - [ ] `__builtin_va_list`
  - [ ] `__gnuc_va_list`
  - [ ] `complex`
  - [ ] `_Complex`
  - [ ] `complex double`
  - [ ] `complex float`
- [ ] Type qualifiers:
  - [x] `signed`, `unsigned`
  - [x] `const`
  - [x] `__const__`, `__volatile__` (GCC extension)
  - [x] `volatile`
- [x] Pointers (`T *`)
- [ ] C++ references (`T &`) (C++ extension)
- [ ] Name lookups
- [ ] `struct foo`, `enum foo`
- [ ] vector types (GCC extension)
- [ ] `struct`/`union` inside types
- [ ] Arrays
- [ ] Zero-sized arrays, structs, unions (GCC extension)
- [ ] Variable length arrays and structs (C99)
- [ ] `__attribute__` (GCC extension)
  - [ ] `aligned`
  - [ ] `packed`
  - [ ] `mode`
  - [ ] `vector_size`
  - [ ] `cdecl`
  - [ ] `fastcall`
  - [ ] `stdcall`
  - [ ] `thiscall`

### Operators

- [x] Arithmetic (`+`, `-`, `*`, `/`, `%`)
- [x] Comparisons (`==`, `!=`, `>`, `<`, `>=`, `<=`)
- [x] AND/OR
- [x] Bitwise opeators (`&`, `|`, `^`, `<<`, `>>`)
- [x] Unary operators (`-`, `+`, `!`, `~`)
- [x] Ternary operator
- [x] Operator precedences
- [ ] `alignof`
- [ ] `sizeof`
- [ ] `__alignof__` (GCC extension)

### Assorted extensions

- [ ] `__extension__` (GCC extension)
- [ ] `__asm__("symbol")` (symbol redirection, GCC extension)
- [ ] `__cdecl`, `__fastcall`, `__stdcall`, `__thiscall` (MSVC)
- [ ] `__ptr32`, `__ptr64` (MSVC)
- [ ] `__declspec(align(n))` (MSVC)
- [ ] `#pragma pack` (MSVC)

## Backend

Implements the functionality provided by the parser.

- [x] Function calls
- [ ] Variadic function calls
- [x] Variable reads and assignments
- [x] Integers up to 32 bits, `float`, `double`
- [ ] `long double`
- [ ] 64-bit integers
- [ ] `time_t`
- [ ] Complex types
- [x] Pointers
- [ ] C++ references
- [x] Enums (except large enums beyond `int`)
- [ ] Correct type conversions (partial)
- [ ] Safer handling of value conversions
- [ ] Typedefs
- [ ] Structs
  - [ ] Bitfields
- [ ] Unions
- [ ] Arrays
- [ ] Vectors
- [ ] Alternate calling conventions
- [ ] Constant expressions
  - [x] Simple integer expressions
  - [ ] Correct integer promotions
  - [ ] Non-integer type support
- [ ] `time_t`

## API

The APIs the FFI should provide.

### Symbols

- [x] `cffi.cdef` (symbol definition)
- [x] `cffi.C` (global namespace)
- [ ] `cffi.load` (library namespaces)

### cdata manipulation

- [x] `cffi.new` (object allocation, minus missing features)
- [x] `ctype` constructors (object allocation, minus missing features)
- [x] `cffi.typeof` (`ctype` creation)
- [ ] `cffi.cast` (`cdata` typecasts)
- [ ] `cffi.metatype` (custom `ctype` metatables)
- [ ] `cffi.gc` (custom `cdata` finalizers)

### ctype manipulation

- [ ] `cffi.sizeof`
- [ ] `cffi.alignof`
- [ ] `cffi.offsetof`
- [ ] `cffi.istype` (type checking interface)

### Utilities

- [ ] `cffi.errno` (portable `errno` handling)
- [x] `cffi.string` (pointer/array to Lua string)
- [ ] `cffi.copy` (`memcpy`)
- [ ] `cffi.fill` (`memset`)
- [ ] `cffi.tonumber` (`cdata`-aware `tonumber`)
- [ ] `cffi.tostring` (`cdata`-aware `tostring`)
- [ ] `cffi.ll` (64-bit signed integer construction)
- [ ] `cffi.ull` (64-bit unsigned integer construction)

### Target information

- [ ] `cffi.abi`
- [ ] `cffi.os`
- [ ] `cffi.arch`

### Callbacks

- [ ] Minimal functionality
- [ ] `cb:free`
- [ ] `cb:set`

## Portability

The main testing environment is `ppc64le` Linux. Other environments are
currently not tested. The code is written to be portable and likely works
already (except on Windows).

- [x] Linux
- [ ] BSDs (untested, may work)
- [ ] Windows
- [ ] macOS (untested, may work)
- [x] Little endian
- [ ] Big endian (untested, may work)

## Compatibility

This deals with Lua version compatibility.

- [ ] Lua 5.1 (untested, but will not work)
- [x] Lua 5.2
- [x] Lua 5.3
- [ ] Lua 5.4 (untested, may work)

## Build system

Build system enhancements TODO.

- [ ] Lua version option
- [ ] Test suite

## Code

Various things to do around the existing code.

- [ ] `dlopen` etc. abstraction
- [ ] `libffi` abstraction
- [ ] Type system cleanup
- [ ] Testing infrastructure
- [ ] ... TBD
