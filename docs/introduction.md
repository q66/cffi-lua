# Introduction

The `cffi-lua` project aims to provide a Lua module that allows calling C
functions and using C data structures from Lua.

The most obvious purpose for this is writing bindings to C libraries. Normally
you would need to create manual glue code in C to expose the functionality
into Lua; this is made unnecessary with an FFI, simply provide it with some
C declarations and you're good to go.

The module largely emulates the API and semantics of the FFI library contained
in LuaJIT (http://luajit.org/ext_ffi.html). It does not aim to be bug-for-bug
compatible, and some Lua semantics cause assorted differences (e.g. comparisons
of C data for equality and so on). Where there are differences, the other
documentation generally makes it clear.

Unlike the LuaJIT FFI, it works with any common Lua version, starting with
Lua 5.1. It works with LuaJIT itself, too, but there isn't much use to that.

## Simple example

```
local cffi = require("cffi")
cffi.cdef [[
    int printf(char const *fmt, ...);
]]
ffi.C.printf("hello %s\n", "world")
```

Note how this is pretty much identical to the example on the LuaJIT website.

Using the FFI consists of three steps:

1) First you need to load the module using `require`
2) Declare the C functions/data structures you wish to use
3) Call the C function using the appropriate namespace

The module internally uses `libffi` (https://sourceware.org/libffi) to ensure
portability. Thanks to that, it can be reasonably portable. It will probably
run on any common architecture and operating system you can think of, no
matter if it's x86, ARM, PowerPC, Linux, macOS or some BSD.

## C data structures

Since the FFI parses plain C declarations, it makes sense it wouldn't stop
at just functions. We can define C structures, and even give the mmetatables,
to make pretty Lua interfaces.

First, let's start with a plain structure:

```
local cffi = require("cffi")
cffi.cdef [[
    typedef struct point_t { double x; double y; } point_t;
]]
```

We can create an instance of such structure with `cffi.new`, and perhaps give
it some default values, too. If no initializer is provided, the fields are
automatically zeroed, not left undefined like in C.

```
local pt = cffi.new("point_t") -- {x = 0, y = 0}
local pt = cffi.new("point_t", 5, 10) -- {x = 5, y = 10}
local pt = cffi.new("point_t", {5, 10}) -- {x = 5, y = 10}
local pt = cffi.new("point_t", {x = 5, y = 10}) -- {x = 5, y = 10}
pt.x = 20
print(pt.x, pt.y)
```

Now let's give it a metatable, for a prettier interface.

```
local point
point = cffi.metatype("point_t", {
    __add = function(a, b)
        return point(a.x + b.x, a.y + b.y)
    end,
    __len = function(a)
        return math.sqrt(a:area())
    end,
    __index = {
        area = function(a)
            return a.x * a.x + a.y * a.y
        end
    }
})
```

The we can use it like this:

```
local pt = point(3, 4)
print(pt.x, pt.y) -- 3, 4
print(#pt) -- 5
print(pt:area()) -- 25
local pt2 = a + point(0.5, 8)
print(#pt2) -- 12.5
```

The `cffi.metatype` takes a C declaration and permanently associates it with
a metatable. This metatable is then used for all instances of that type. You
can use all metamethods you could otherwise use in that Lua version on other
types.

You should not modify the metatable once it has been associated. You can not
normally retrieve it, it's protected; it is still possible to do that via
the `debug` interface, but keep in mind that the metamethods that have been
set in the metatable are registered with the type during binding, therefore
anyhow changing the set of metamethods in the table will either not take
effect or will break.

Otherwise, you can refer to the LuaJIT FFI documentation for most things.
Just keep in mind the potential differences.

## C idioms

These are like in LuaJIT.

### Pointer dereference

Lua has no dereference operator.

For:

```
int *p;
```

With:

```
x = *p;
*p = y;
```

Use:

```
x = p[0]
p[0] = y
```

### Array and pointer indexing

For:

```
int i, a[]; // or int i, *a;
```

With:

```
x = a[i];
a[i] = y;
```

Use:

```
x = a[i]
a[i] = y
```

### Struct/union field access (value or pointer)

For:

```
struct foo s; // or struct foo *s;
```

With:

```
x = s.field; // or x = s->field;
s.field = y; // or s->field = y;
```

Use:

```
x = s.field
s.field = y
```

### Pointer arithmetic

For:

```
int *p, i;
```

With:

```
x = p + i;
y = p - i;
```

Use:

```
x = p + i
y = p - i
```

### Pointer difference

For:

```
int *p1, *p2;
```

With:

```
d = p1 - p2;
```

Use:

```
d = p1 - p2
```

### Pointer to array element

For:

```
int i, a[];
```

With:

```
x = &a[i];
```

Use:

```
x = a + i;
```

### Cast pointer to address

For:

```
int *p;
```

With:

```
x = (uintptr_t)p;
```

Use:

```
x = ffi.cast("uintptr_t", x)
```

### Out-arguments

**Different from LuaJIT.** Well, you can use the same approach with allocating
a single-element array if you want. We have an extension for this, though.

For:

```
void foo(int *oarg);
```

With:

```
int x = ...;
foo(&x);
y = x;
```

Use:

```
local x = cffi.new("int", ...)
foo(cffi.addressof(x))
y = x;
```

### Vararg conversions

For:

```
int printf(char const *fmt, ...);
```

With:

```
printf("%g", 1.0);
printf("%d", 1);
```

Use:

```
printf("%g", 1.0)
printf("%d", ffi.new("int", 1))
```

**This applies in the default Lua configuration.** If your Lua is configured
to use a different type for numbers, the conversions may be different; see
the [semantics.md](semantics.md) document.

**With Lua 5.3, integers will always get converted to an integer type, not
a floating point type.** That means passing `1.0` and `1` is different in 5.3,
while it's the same in `5.2` and older.

## Caching

**The caching advice from LuaJIT does not apply here.** LuaJIT suggests to
cache only namespaces, without caching C functions themselves, as it has a JIT
engine that can optimize these out into direct calls. We have no such thing,
and each namespace index call has its own overhead (it has to retrieve the
symbol). Calls don't concern us; they are always indirect, or well, they have
to go through Lua before getting to `libffi`.

Therefore, it is advisable to cache function handles when you can.

```
-- definitely do this
local foo = cffi.C.foo
local bar = cffi.C.bar
local baz = function(a, b)
    return foo(a) + bar(b)
end
```

This is the fastest you can get. As for caching `ctype`s, the usual LuaJIT
advice still applies.
