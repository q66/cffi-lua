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

Just like in LuaJIT, creating a `cdata` object with `cffi.new` (or the `ctype`
constructor syntax, which is functionally identical) always initializes its
contents. Depending on the given initializers, different rules apply.

- Without any initializers, the memory is zeroed (`memset(p, 0, nbytes)`)
- Scalar types such as numbers and pointers accept a single initializer. The
  given initializer is converted to the scalar C type.
- (*not yet implemented*) Complex numbers (and vectors) are treated like
  scalars with a single initializer, otherwise like arrays.
- Aggregate types (structs and arrays) accept either a single `cdata`
  initializer of the same type (copy constructor), a single table initializer,
  or a list of initializers (each element is initialized with one argument)
- The elements of an array are initialized (starting at index zero). If a
  single initializer is given, it is repeated for all remaining elements.
  If two or more initializers are given, only the given fields are initialized,
  with the remaining ones getting filled with zero bytes. If too many are given,
  an error is raised.
- Byte arrays may also be initialized with a Lua string. The string is copied
  including its terminating zero, and no errors are raised on size mismatch.
- Only the first field of a `union` can be initialized with a flat initializer.
- Elements or fields which are aggregates are initialized with a single
  initializer, which follows the standard rules.
- Excess initializers raise an error.

## Table initializers

This is also identical to LuaJIT. Only arrays, `struct`s and `union`s can be
initialized with a table.

- If the table index `[0]` is not `nil`, then the table is assumed to be zero
  based, otherwise it's one based.
- Array elements, starting at index zero, are initialized one-by-one with
  the consecutive table elements. The moment a `nil` element is reached, the
  initialization stops.
- If exactly one array element was initialized, it's repeated for all remaining
  elements. Otherwise, all remaining elements are filled with zero bytes.
- The above logic does not apply for VLAs, which are only initialized with
  the elements given in the table.
- A `struct` or `union` type can be initialized in the order of the declaration
  of its fields. Each field is initialized like it was an array and the process
  stops at the first `nil`.
- Otherwise, if neither `[0]` or `[1]` are present, a `struct`/`union` is
  initialized by looking up each field in the table by name. Each non-`nil`
  field is initialized.
- Uninitialized fields of a `struct` are filled with zero bytes, except for
  flexible array members.
- Initialization of a `union` stops after one field has been initialized. If
  none has been initialized, the `union` is filled with zero bytes.
- Elements that are aggregates are each initialized with a single initializer,
  which may be a table.
- Excess initializers raise an error for arrays, but not for `struct`s or
  `union`s. Unrelated table entries are ignored.

## Operations on cdata

Generally identical to LuaJIT, with some caveats. Standard Lua operators can
generally be applied to `cdata` objects or a mix of `cdata` and Lua object.

Reference types are dereferenced before performing their operations. The
operation is applied to the C type pointed to by the reference.

The pre-defined operations are always tried first, following standard Lua
semantics. If that is not possible, a metamethod is used (or an `__index`
table). An error is raised if the metamethod lookup or index table lookup
fails.

### Indexing a cdata

- **Pointers/arrays**: a `cdata` pointer/array can be indexed by a `cdata`
  number or a Lua number. The element address is computed like in C. Read
  access will convert the element value to a Lua object. Write access will
  convert the Lua object to the element type and store it in the array. An
  error is raised if the element size is undefined or write access to a
  constant element is attempted.
- **Accessing struct/union fields**: a `cdata` `struct`/`union` or a pointer
  to it can have its fields accessed by name. The field address is computed
  like in C. Read access will convert the element value to a Lua object,
  write access will convert the Lua object to the element type and store it.
  An error is raised if either the aggregate or the field is constant.
- **Indexing a complex number**: (*not yet implemented*) a complex number
  can be indexed by a `cdata` number or a Lua number with the values 0 or 1,
  or by the strings `re` or `im`. Read access loads the real part or the
  imaginary part and converts it to a Lua number. The sub-parts of a complex
  number are immutable. Accessing out-of-bounds elements is undefined but
  will not trigger a segfault.
- **Indexing a vector**: (*not yet implemented*) a vector is treated like an
  array for indexing, except the elements are immutable.

A `ctype` object can be indexed with a key as well. The only defined operation
is accessing constant fields. All other accesses will trigger metamethods.

**Difference from LuaJIT**: Since we have an address-of function, you can
modify contents of value types after they are created, by taking their address
and indexing them.

### Calling a cdata

- **Constructor**: a `ctype` object can be called and used as a constructor.
  This is equivalent to `cffi.new(ct, ...)` unless a `__new` metamethod is
  defined, in which case it's called instead. Note that inside you have to
  use `cffi.new` directly in order not to cause infinite recursion.
- **C function call**: a `cdata` function or a function pointer can
  be called. The passed arguments are converted to C types as required
  by the declaration. Arguments passed to the vararg part undergo special
  conversion rules. The C function is called and the return value is converted
  to a Lua object, losslessly.

**Difference from LuaJIT**: Windows `__stdcall` functions must be explicitly
declared as so.

### Arithmetic on cdata

- **Pointer arithmetic**: a `cdata` pointer/array and a `cdata` number or a
  Lua number can be added or subtracted. The number must be on the right hand
  side. The result is a pointer of the same type with the address changed the
  same way as in C. An error is raised if the element size is not defined.
- **Pointer difference**: two compatible pointers/arrays can be subtracted.
  The result is a difference in their addresses, divided by their element size
  in bytes, thus effectively the number of elements. An error is raised if
  the element size is unknown or zero.
- **64-bit integer arithmetic**: the standard arithmetic operators can all
  be applied to two `cdata` numbers or a mix of `cdata` number and Lua number.
  If one of them is an unsigned 64-bit integer, the other is ocnverted to
  the same type and the operation is unsigned. Otherwise, both sides are
  converted to a signed 64-bit `cdata` and a signed operation is performed.
  The result is a boxed 64-bit `cdata` object.

Not yet implemented: if one side in arithmetic is an `enum` and the other side
is a string, the string is converted to the value of a matching `enum` before
the conversion, the result is still a 64-bit `cdata` integer though.

You will explicitly have to convert `cdata` numbers with `cffi.tonumber`. This
may incur a precision loss, at least depending on your Lua version and/or
configuration (5.3+ integers are respected).

### Comparisons of cdata

- **Pointer comparison**: two compatible `cdata` pointers/arrays can be compared.
  The result is the same as unsigned comparison of their addresses. The `nil`
  value is treated like a `NULL` pointer, compatible with any other pointer type.
- **64-bit integer comparison**: two `cdata` number or a `cdata` number and a
  Lua number can be compared. The same conversions as in arithmetic are
  performed first, same with `enum`s.
- **Equality comparisons** never raise an error, but a notable **difference
  from LuaJIT** is that metamethods are only ever triggered on compatible
  Lua types, which means comparisons against `nil` will always be `false`
  no matter what. Incompatible `cdata` pointers can always be tested for
  address equality.

### Table keys and cdata

Do not use `cdata` as table keys. Since they are `userdata` objects to Lua,
they will always be handled by address of the Lua object, which means not
even any two scalar `cdata` objects will ever hash to the same value.

For numbers, if you can deal with the precision of numbers in your Lua version,
that may be an option. Especially with Lua 5.3 and its integer support, you
should not get any precision loss by default.

You can also always create your own hash table with the FFI, which will allow
indexing by `cdata`.

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
