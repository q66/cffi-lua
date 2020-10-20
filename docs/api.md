# FFI API

This document describes the API provided by the FFI module. It aims to stay
mostly comparible with LuaJIT's FFI: http://luajit.org/ext_ffi.html

However, there is a number of differences.

## Terms

Largely identical to LuaJIT.

- `cvalue` - Either `ctype` or `cdata`
- `cdata` - A `cvalue` holding a value with the matching `ctype`
- `ctype` - A kind of `cvalue` that has a type but not a value
- `cdecl` - A string holding a C type declaration
- `ct` - A C type specification, either a `cdecl`, `ctype` or `cdata`
- `cb` - A callback object, this is a kind of `cdata` function pointer
  that actually points to a native Lua function
- `VLA` - A variable length array, or an array with `?` in place of element
  count; the number of elements must be provided during creation
- `VLS` - A `struct` with a flexible array member

Differences from LuaJIT API will be clearly marked as such.

## Declaring and accessing symbols

There are two steps to accessing external symbols. First they must be declared
with `cffi.cdef`, then accessed through the appropriate namespace.

### cffi.cdef(def [, params...])

This function takes one or more C declarations as a string and declares them
within the FFI. The string is actually a chunk of C code. Common C syntax
is generally supported, please refer to [syntax.md](syntax.md) for supported
syntax.

There is no preprocessor and the parser doesn't support any C preprocessor
tokens. Otherwise, the syntax support is reasonably complete. You could use
an external preprocessor if you wish.

The best way to use this function is with the Lua syntax sugar for function
calls with one string argument:

```
cffi.cdef [[
    /* some function */
    void foo();
    // another function
    int bar(float);
]]
```

Declarations are not subject to namespacing; all declarations apply to all
namespaces, their existence is then checked at runtime. There is no separation
between individual `cdef` calls, declarations made in one call carry over.

Any errors in the declaration (e.g. syntax errors) will be propagated as Lua
errors and any declarations staged during the call will be discarded. You do
not have to worry about partial declarations leaking through.

**Note:** The `cdef` function supports parameterized types. Read up on those
in the [semantics.md](semantics.md) document. The extra parameters are used
with those.

### cffi.C

The default C library namespace, bound to the default set of symbols available
on your system.

On POSIX systems, this will contain all the symbols available by default, which
includes the C standard library and related things (on Linux, `libm`, `libdl`
and so on), possibly `libgcc` as well as symbols exported from Lua.

On Windows, symbols are looked up in this order:

- The current running executable
- The executable or library containing the FFI (`cffi.dll` when a module
  or a dynamically linked library, or the executable when compiled in)
- The C runtime library
- `kernel32.dll`
- `user32.dll`
- `gdi32.dll`

Keep in mind that C standard library symbols such as `stdio.h` symbols may
not always be exported - e.g. MinGW defines them as inline functions. For
Microsoft toolchain, we have an override in place that forces `stdio`
symbols to be exported, but it's not possible on other toolchains.

### clib = cffi.load(name, [,global])

This loads a dynamic library given by `name` and returns a namespace object
that you can access the library symbols through.

The `name` argument can be a path, in which case the library is loaded from
the path, otherwise it is treated in a system-specific way and searched for
in default path(s). Generally that means the following:

On POSIX systems, if the name does not contain a dot, the `.so` extension is
appended (`.dylib` on macOS). The `lib` prefix is also prepended, if necessary.
Therefore, calling something like `cffi.load("foo")` will look for `libfoo.so`
(or `libfoo.dylib`) in the default search path.

On Windows systems, if there is no dot in the name, the `.dll` extension is
appended. Therefore, calling `cffi.load("foo")` will look for `foo.dll` in the
default path.

## Creating cdata objects

The following functions create `cdata` objects. All created `cdata` objects
are subject to garbage collection.

### cdata = cffi.new(ct, [,nelem] [,init...])

This creates a `cdata` object for the given `ct`. VLA/VLS types require the
`nelem` argument and will raise an error if that is not given.

The `cdata` object is initialized using standard initializer rules described
in [semantics.md](semantics.md), using the optional `init` argument(s). Excess
initializers will raise an error.

Keep in mind that anonymous `struct` or `union` declarations in `ct` will
create a new declaration every time, which is probably not something you want.
Use `cffi.typeof` to cache the declaration, then use that.

### cdata = ctype([nelem,] [init...])

This is fully equivalent to `cffi.new`, but using an object previously returned
from `cffi.typeof` or other means.

### ctype = cffi.typeof(ct [, params...])

Creates a `ctype` object for the given `ct`. This is largely useful to parse
the `ct` argument once and return a permanent handle to it, which you can use
with a constructor like above.

**Note:** The `typeof` function supports parameterized types. Read up on those
in the [semantics.md](semantics.md) document. The extra parameters are used with
those.

### cdata = cffi.cast(ct, init)

This creates a new `cdata` object using the C type cast rules. See the right
section in [semantics.md](semantics.md) for more information. In some cases
it will be the same as `cffi.new`, but things like pointer compatibility checks
are ignored.

### ctype = cffi.metatype(ct, metatable)

This creates a `ctype` object for the given `ct` and associates it with a
metatable.

This is only supported for `struct`/`union` types. You can't change the
metatable once assigned, an error will be raised.

**Difference from LuaJIT:** you can reassign the contents of the metatable
later, but only as long as the actual fields do not change. The FFI internally
builds a mapping of metamethods that have been assigned to the `ctype`, thus
adding more metamethods into the table after the assignment will have no
effect (they will never be used) and removing some will result in calls to
`nil` values, which you do not want.

All metamethods implementable for `userdata` in the given Lua version are
supported. That means at very least those supported by Lua 5.1, plus anything
added later (5.2 supports `__pairs` and `__ipairs`, 5.3 adds bitwise ops).

When using Lua 5.3 or later, `__name` from the metatable is used when calling
`tostring`. Its type must be `string`, any other type is ignored, following the
standard Lua semantics (this includes numbers, i.e. `type(v) == "string"` must
be true). For `ctype` objects, the result of `tostring` will be the value of
`__name`, for `cdata` objects the `__name` will replace the C type signature
of the result (i.e. you will get something like `foo: 0xDEADBEEF`).

With Lua 5.4, the `__close` metamethod for to-be-closed variables is also
supported. On `ctype` objects, it is ignored, as if it didn't exist; on
`cdata` objects it follows the standard semantics.

**Difference from LuaJIT:** because of how metamethods in Lua work, the
`__eq` metamethod will never be called if any of the sides is not `userdata`.
That means checking equality against `nil` or any Lua value will always return
`false` regardless of your implementation, so keep that in mind.

Like in LuaJIT, the `__gc` metamethod mostly results in implicit `ffi.gc`
call during creation of any `cdata` of that type. Therefore, you can still
assign a custom finalizer to specific `cdata` objects.

### cdata = cffi.gc(cdata, finalizer)

Associates a finalizer with `cdata`. The finalizer will be called on the
object during garbage collection and can be any callable object. The obvious
usage case is automatically freeing pointers allocated with e.g. `malloc`,
like this:

```
local p = cffi.gc(cffi.C.malloc(100), cffi.C.free)
p = nil -- will be garbage collected and finalizer will be called
```

**Difference from LuaJIT:** Can be used with any `cdata`.

### cdata = cffi.addressof(cdata)

**Extension, does not exist in LuaJIT.**

For pointer reference (`T &`) `cdata`, this returns a pointer cdata `T *` with
the same address.

For any other `cdata` (`T`), this takes an address to that and returns a `T *`.

## C type information

### size = cffi.sizeof(ct, nelem)

Returns the size of `ct` in bytes. Never raises an error, if the size is not
known, returns `nil` (e.g. for `void` or function types). If `ct` is a type
or a declaration string specifying VLA/VLS, `nelem` is required. Otherwise
if it is a VLA/VLS `cdata`, the size is determined automatically.

### align = cffi.alignof(ct)

Returns the minimum required alignment of the `ct`.

### offset = cffi.offsetof(ct, field)

If `field` does not exist in `ct`, returns `nil`. Otherwise, it returns the
offset (in bytes) within the aggregate (`struct`/`union`) type for the member.

**Difference from LuaJIT:** Since we do not support bit fields, the special
case of returning the position and offset for those is not handled.

### bool = cffi.istype(ct, obj)

Returns `true` if `obj` has the C type given by `ct`. Otherwise returns `false`.

C type qualifiers (`const` and so on) are ignored. Pointers are checked using
the standard rules, but without a special case for `void *`. If `ct` specifies
an aggregate type, then a pointer to this is also accepted, otherwise the match
must be exact.

For non-`cdata` objects this always returns `false`.

## Utility functions

### err = cffi.errno([newerr])

Returns the `errno` number set by the last C call and optionally assigns a
new value specified by `newerr`.

This function exists in order to portably handle `errno`. You should call this
as close to the C function that sets it as possible to make sure it is not
overridden by something else.

### str = cffi.string(ptr [,len])

Creates a Lua string from the data pointed to by `ptr`.

If the optional `len` argument is not specified, `ptr` is converted to `char *`
and the data is assumed to be zero terminated.

Otherwise `ptr` is converted to `void *` and `len` is used to specify the number
of bytes. The string may contain embedded zeroes and does not necessarily need
to be byte oriented (but be aware of potential endianness issues).

The main purpose of this function is to convert string pointers returned from C
to Lua strings. The resulting Lua string is a standard interned string, unrelated
to the original.

### cffi.copy(dst, src, len)

This is pretty much an equivalent of `memcpy`. Accepts a destination pointer,
a source pointer and length. The `dst` is converted to `void *` while the `src`
is converted to `void const *`.

The `src` input can also be a Lua string.

### cffi.copy(dst, str)

Given a Lua string, this copies its contents into `dst`. Equivalent to calling
`cffi.copy(dst, str, #str)`.

### cffi.fill(dst, len [,c])

Fills the data pointed to by `dst` with `len` constant bytes, given by `c`. If
`c` is not provided, the data is filled with zeroes.

### val = cffi.toretval(cdata)

**Extension, does not exist in LuaJIT.**

Given a `cdata`, this converts it to a Lua value (which may or may not be
`cdata`) in a manner identical to if the type of the `cdata` was returned from
a C function and called through the FFI.

In general this means things fully representable using Lua values (most numbers
and so on) will be returned as the appropriate Lua types, while others will
be returned as `cdata`.

### val = cffi.eval(str)

**Extension, does not exist in LuaJIT.**

Given a Lua string, this attempts to evaluate the string as a C constant
expression and returns the `cdata` created from that. This is useful to
create 64-bit integer `cdata` without having the LuaJIT parser extensions,
as Lua numbers don't have enough precision to represent all values.

### cffi.nullptr

**Extension, does not exist in LuaJIT.**

As Lua metamethod semantics do not allow equality comparisons of different
types, LuaJIT style comparisons `cdata == nil` cannot be done. Instead, this
constant is provided and used like `cdata == cffi.nullptr`.

It is just a `void *` `cdata` with address `0x0`.

## Target-specific information

### bool = cffi.abi(param)

Returns `true` if `param` (which is a Lua string) applies to the target ABI,
and `false` otherwise. The following parameters are defined:

| Parameter | Description             |
|-----------|-------------------------|
| 32bit     | 32-bit architecture     |
| 64bit     | 64-bit architecture     |
| be        | Big endian              |
| le        | Little endian           |
| win       | Windows ABI             |
| eabi      | 32-bit ARM EABI         |
| uwp       | Windows UWP             |
| elfv2     | 64-bit POWER ELFv2      |
| fpu       | Hardware FPU            |
| hardfp    | Hard float ABI          |
| softfp    | Soft float ABI          |

**Extensions:** The `elfv2` parameter has been added. The others match LuaJIT.

### cffi.os

The target OS name. Can be `Windows`, `Linux`, `OSX`, `BSD`, `POSIX` or `Other`.
These values exactly match LuaJIT.

### cffi.arch

The target architecture name.

| Name         | Architecture    |
| -------------|-----------------|
| `x86`        | IA-32           |
| `x64`        | x86\_64         |
| `arm`        | 32-bit ARM LE   |
| `armeb`      | 32-bit ARM BE   |
| `arm64`      | AArch64 LE      |
| `arm64be`    | AArch64 BE      |
| `ppc`        | PowerPC BE      |
| `ppcle`      | PowerPC LE      |
| `ppc64le`    | 64-bit POWER LE |
| `ppc64`      | 64-bit POWER    |
| `mips`       | MIPS BE         |
| `mipsel`     | MIPS LE         |
| `mips32r6`   | MIPS R6 BE      |
| `mips32r6el` | MIPS R6 LE      |
| `mips64`     | MIPS64 BE       |
| `mips64el`   | MIPS64 LE       |
| `mips64r6`   | MIPS64 R6 BE    |
| `mips64r6el` | MIPS64 R6 LE    |
| `unknown`    | Other           |

This matches LuaJIT.

## Callback methods

### cb:free()

Frees the resources associated with the callback. The Lua function is unanchored
and may be garbage collected. The callback handle is no longer valid and must
not be called anymore (an error will be raised).

**Difference from LuaJIT:** For now the callback handles are not reused. This
may change in the future, so you should assume they could be.

### cb:set(func)

Associates a new Lua function with the callback. The old function is unanchored
and the new function takes its place. This is useful so you can reuse callback
resources without allocating a new closure every time, which is fairly expensive.

## Standard cdata metamethods

The default `cdata` metatable implements all possible metamethods available in
this Lua version. This is necessary in order to be able to call custom
metamethods set through `cffi.metatype`. Most of them will raise errors
if there is no default behavior for them.

Arithmetic and comparison metamethods have default implementations that
work on relevant `cdata`, as described in [semantics.md](semantics.md).

The `__index` and `__newindex` metamethods are implemented by default for
pointer, reference, array and aggregate type `cdata`. The semantics of those
are described in [semantics.md](semantics.md).

### mt(cdata).__tostring

Normally, this will return `cdata<T>: 0xADDRESS`. There is a special case for
64-bit integer `cdata`, which instead return the numeric value, with the
appropriate `LL` or `ULL` suffix. The address of the `cdata` in the default
printing is either the pointer value (if pointer-like) or the address of the
actual value.

Ctypes also have an implementation, they will return `ctype<T>`.

### mt(cdata).__metatable

This has the value `ffi`. It is used to prevent retrieval of the default `cdata`
metatable through standard interfaces, instead the string `ffi` will always be
returned. You can still inspect the metatable through the `debug` interfaces
but it is not recommended to do so. Any changes done to it may break the FFI.

## Extended Lua functions

### n = cffi.tonumber(v)

If given a `cdata`, this is converted to a Lua number. This may incur a
precision loss, particularly on pre-5.3 Lua without integer support. Returns
`nil` if the `cdata` cannot be converted (isn't numeric).

If given a non-`cdata`, performs a conversion according to the standard Lua
semantics of `tonumber`.

### tp = cffi.type(v)

Like Lua `type()`, but returns `cdata` for `cvalue`s (regular Lua `type()`
will return `userdata`). Returns the same thing as the standard function
for anything else.
