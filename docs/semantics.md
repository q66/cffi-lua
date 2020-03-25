# FFI semantics

This document aims to describe the semantics of the FFI as closely as possible.

In general, we mimic the LuaJIT FFI semantics as well as the C language
semantics. Sometimes this is not possible, the sections where this differs
will be clearly marked as such.

## C language support

You can refer to `syntax.md` for this, which covers all of the syntax
implemented by the FFI at this point.

Our C parser aims to be simple first and foremost. Therefore, it does not
have detailed error message analysis and so on. It will, however, reject
invalid declarations, and likely is stricter than LuaJIT's.

## Conversion rules

## Initializers

## Table initializers

## Operations on cdata

## Parameterized types

Just like LuaJIT, our FFI supports parameterized types in `cffi.typeof` and
`cffi.cdef`. In whichever C context where a type, a name or a number is
expected, you can write `$`. These placeholders are then replaced with
extra arguments pased.

```
cffi.cdef([[
    typedef struct { $ $; } foo_t;
]], cffi.typeof("int"), "foo")

local bar_t = cffi.typeof("struct { int $; }", "foo")
local bar_ptr_t = cffi.typeof("$ *", bar_t)

-- works even where VLAs won't
local multi_t = cffi.typeof("uint8_t[$][$]", w, h)
```

Like in LuaJIT, parameterized types are not simple text substitution. Where
a type is expected, you must supply a `ctype` or `cdata`, you can't simply
pass a string.

You can think of it as something similar to C++ templates.

Parameterized types are also useful to refer to anonymous `ctype`s where you
can't refer to them by name (because they're, well, anonymous). As long as
you have a Lua handle to the anonymous `ctype`, you can still pass it in
a parameterized context.

Same precautions as in LuaJIT generally apply. Use them sparingly.

## Garbage collection of cdata

All `cdata` objects are garbage collected `userdata`. That means when you
allocate memory, e.g. with `cffi.new`, you need to make sure to retain this
somewhere in Lua for as long as it stays on C side. Once the last reference
is gone, the memory may be freed arbitrarily.

```
cffi.cdef [[
    void foo(int *p);
]]
cffi.C.foo(cffi.new("int[10]")) -- bad, may be GC'd
local p = cffi.new("int[10]")
cffi.C.foo(p) -- correct
```

Note that for pointers, the address itself is that memory, not what it points
to. So when you e.g. use `cffi.C.malloc` to allocate a block of memory, it
will not be garbage collected, only its `cdata` pointer object will.

In those cases, you need to manually manage the memory in order not to leak
it, though. You can use `cffi.gc` to associate it with a finalizer. Then
the manually allocated memory will be garbage collected in the same manner.

## Callbacks

The FFI provides a callback API that is identical to LuaJIT's. Whenever you
convert a Lua function to a C function pointer, a callback is allocated. The
resulting function pointer can then be called by C or by Lua.

Callback creation can happen implicitly (e.g. by passing Lua functions) or
you can explicitly use `cffi.cast` (or `cffi.new`) to create a callback.

Keep in mind that **vararg functions are not supported as callbacks**. You
also **can pass a struct by value**, unlike LuaJIT, but because of limitations
of `cffi` itself, you can't pass `union`s by value, just like with standard
calls.

There are no limitations or checks on the Lua function. The arguments passed
to the Lua function will undergo conversions according to their standard
rules, without a precision loss. There is no reasonable way to make sure
argument count matches and so on, so keep that in mind.

### Callback resources

Unlike LuaJIT, there is no artificial limit on how many callbacks you can have
at a time.

Just like in LuaJIT, callbacks are permanent, as the same lifetime concerns
apply. You will want to manually manage the callback memory when used
frequently.

```
cffi.cdef [[ ... ]]
local cb = cffi.cast("cb_t", function(...) ... end)
cffi.C.func(cb)
-- we asume func doesn't store the callback and doesn't need it anymore
cb:free() -- callback no longer valid, can't be used
```

In general, you should still avoid callbacks when you can. There is always
a cost to them. Also, use `cb:set` to reuse callbacks of the same type when
you can. That way we can avoid allocating a new callback every time.

## Library namespaces

A namespace is a special `userdata` that allows access to C functions and
other symbols. The `cffi.C` namespace always exists. Other namespaces are
created by the user when loading libraries.

Indexing a namespace will result in the appropriate symbol being retrieved
and provided to you as `cdata`. Symbols must always be declared first, with
`cffi.cdef`. Both undeclared and missing symbols will result in errors being
raised.

This is what happens on reads:

- Functions: a `cdata` object with the type of the function is returned
- Variables: the symbol undergoes conversion to a Lua object according
  to the rules defined above; this never undergoes precision loss, so
  you may get either a Lua value or a `cdata`
- Constants: undergoes conversion to a Lua object, without precision loss

This is what happens on writes:

- Variables: the vaůie undergoes conversion to its C type according to the
  rules defined above and the resulting value is written
- Other declarations: an error is raised

C library namespaces themselves are garbage collected, once the last reference
is gone, the namespace will eventually be released. This may result in existing
symbol pointers to be invalidated, so keep that in mind and keep your library
namespace alive for as long as any symbols you may be using.

Unlike LuaJIT, we don't have a JIT engine, so caching C functions from
namespaces is useful, to avoid the overhead of the retrieval.

## Other precautions

Keep in mind that just like LuaJIT's FFI, this is a low level library and
provides no memory safety, bounds checking or other measures. Therefore, you
should probably keep this away from high level code when writing bindings, as
it is easily possible to segfault the program and so on from Lua code.

**Do not allow FFI access to untrusted Lua code.** You will want to sandbox
this away, always.
