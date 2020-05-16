# FFI semantics

This document aims to describe the semantics of the FFI as closely as possible.

In general, we mimic the LuaJIT FFI semantics as well as the C language
semantics. Sometimes this is not possible, the sections where this differs
will be clearly marked as such.

## C language support

You can refer to [syntax.md](syntax.md) for this, which covers all of the
syntax implemented by the FFI at this point.

Our C parser aims to be simple first and foremost. Therefore, it does not
have detailed error message analysis and so on. It will, however, reject
invalid declarations, and likely is stricter than LuaJIT's.

## Conversion rules

Conversions follow the same rules as LuaJIT. Since the LuaJIT documentation
on those is relatively confusing, it is described with more detail here.

### C types -> Lua values

There are several contexts in which C types are converted to Lua values. In
each context, a conversion rule is used, which influences the manner in which
the value is converted.

- Return values from C function calls (*return rule*)
- Passing arguments to Lua callbacks (*pass rule*)
- Reading external C variables (*return rule*)
- Reading constants (*return rule*)
- Reading array elements and aggregate fields (*conversion rule*)
- Converting `cdata` to Lua numbers with `cffi.tonumber` (*conversion rule*)
- Using `cffi.toretval` (*return rule*)

It generally applies that conversions are lossless. That means there is no
precision loss when creating the Lua value; in practice it means e.g. when
dealing with large integers, they will be converted to Lua numbers but only
when the Lua number can fit the entire value losslessly, otherwise you will
get a boxed `cdata`.

There is only one lossy conversion and that's `cffi.tonumber`.

Here is a table of all conversions:

| Input C type     | Output Lua type   | Context                     |
|------------------|-------------------|-----------------------------|
| bool             | `boolean`         | any                         |
| any numeric      | `number`          | lossy or representable      |
| any numeric      | numeric `cdata`   | lossless, not representable |
| pointer          | pointer `cdata`   | any                         |
| `va_list`        | `va_list` `cdata` | any                         |
| reference        | see below         | conversion rule             |
| reference        | reference `cdata` | any other                   |
| array            | reference `cdata` | any applicable              |
| `struct`/`union` | reference `cdata` | conversion rule             |
| `struct`/`union` | value `cdata`     | any other rule              |

In practice, this means that scalar types are always converted either to
a matching Lua value type or a value `cdata`, while other types are converted
to their matching `cdata`. An exception to this is when accessing `struct` or
equivalent fields/elements in aggregates, which will return a reference `cdata`
to the base type.

**Note**: accessing references in *conversion rule* contexts is special: they
get dereferenced first, and the conversion rule applies to the base type. The
only notable case of this is the fairly niche case of having reference fields
in structs. E.g. accessing an `int &` field in a `struct` will undergo standard
numeric conversion.

### Lua values -> C types

Just like with the opposite conversions, there are several contexts in which
Lua values are converted to C types. In each context, a conversion rule is
used, and these are similar to the conversion rules for the opposite operation.

- Return values from Lua callbacks (*return rule*)
- Passing arguments to C function calls (*pass rule*)
- Initializing array elements/aggregate fields (*conversion rule*)
- Writing into external C varialbes (*conversion rule*)
- Writing values to array elements and aggregate fields (*conversion rule*)
- Allocating `cdata` with `cffi.new` (*conversion rule*)
- Casting `cdata` with `cffi.cast` (*cast rule*)

These conversions are always lossless.

Here is a table of all conversions:

| Input Lua type  | Ouput C type     | Context                   |
|-----------------|------------------|---------------------------|
| `number`        | see note         | any                       |
| `boolean`       | `bool`           | any                       |
| `nil`           | `NULL`           | any                       |
| `userdata`      | `void *`         | any                       |
| `lightuserdata` | `void *`         | any                       |
| `io.* file`     | `FILE *`         | any                       |
| `string`        | `enum`           | match against `enum` type |
| `string`        | byte array       | initializer               |
| `string`        | `char const[]`   | any                       |
| `function`      | function pointer | callback creation         |
| `table`         | C aggregate type | table initializer         |
| `cdata`         | C type           | see C type conversions    |

For `cdata` conversions, see the C type conversions section below.

Lua numbers are converted to their matching `cdata` scalar. The type depends
on your Lua configuration. For default configurations of Lua 5.2 and older,
you will always get a `double`. Lua 5.3 added support for integer types,
so **in 5.3 and newer**, you will get either an integer type or a floating
point type. You may also get an integer type in 5.2 and older for special
configurations that configure numbers as integers.

Please keep in mind that unusual configurations are not as well tested as
regular configurations, but it is still attempted to support them on best
effort basis.

Strings may be converted to arbitrary byte arrays in initializers (with
`cffi.new`) but not in any other context.

Casting (with `cffi.cast`) only supports scalar types (numeric, pointers,
references).

Callback creation may happen in any context, but you will not be able to
manage the callback's resources afterwards, so you should use `cffi.cast`
or `cffi.new` to create an accessible handle first.

References are dereferenced in context of conversions and casts. The operation
is applied on their underlying type.

### C type conversions

These are similar to standard C/C++ conversion rules. Conversions not
mentioned here will error.

These conversions may apply in any context:

| Input   | Output  | Context                                 |
|---------|---------|-----------------------------------------|
| numeric | numeric | any; may undergo narrow/trunc/extend    |
| numeric | `bool`  | any; `false` when `0`, `true` otherwise |
| `bool`  | numeric | any; `1` for `true`, `0` for `false`    |

These conversions only apply in *cast rule* context:

| Input            | Output    | Context                        |
|------------------|-----------|--------------------------------|
| pointer          | reference | address init; no compat checks |
| `io.* file`      | reference | address init; no compat checks |
| `userdata`       | reference | address init; no compat checks |
| `string`         | reference | address init; no compat checks |
| `struct`/`union` | reference | address init; no compat checks |
| array            | reference | address init; no compat checks |
| integer          | reference | address value initialization   |
| pointer          | pointer   | address init; no compat checks |
| `io.* file`      | pointer   | address init; no compat checks |
| `userdata`       | pointer   | address init; no compat checks |
| `string`         | pointer   | address init; no compat checks |
| `struct`/`union` | pointer   | address init; no compat checks |
| array            | pointer   | address init; no compat checks |
| integer          | pointer   | address value initialization   |
| pointer          | funcptr   | address init; no compat checks |
| C function       | funcptr   | address init; no compat checks |

Note that references passed to casts are dereferenced first and the underlying
type is used for conversion. Also, pointers mean both regular and function
pointers in cast inputs.

Only scalar types are allowed as target type of `cast`.

These conversions only apply in *conversion rule* context:

| Input            | Output    | Context                        |
|------------------|-----------|--------------------------------|
| pointer          | reference | address init; checked          |
| `io.* file`      | reference | address init; checked          |
| `userdata`       | reference | address init; checked          |
| `string`         | reference | address init; checked          |
| `struct`/`union` | reference | address init; checked          |
| array            | reference | address init; checked          |
| pointer          | pointer   | address init; checked          |
| `io.* file`      | pointer   | address init; checked          |
| `userdata`       | pointer   | address init; checked          |
| `string`         | pointer   | address init; checked          |
| `struct`/`union` | pointer   | address init; checked          |
| array            | pointer   | address init; checked          |
| pointer          | funcptr   | address init; checked          |
| C function       | funcptr   | address init; checked          |
| `struct`/`union` | same      | must be same type              |

In this context, you can generally create references from pointers as well as
pointers from arrays, but the underlying type has to be compatible, which means
both that the type itself has to match and the cv-qualifiers have to be
compatible (e.g. `char *` to `char const *` conversion is possible but not
the other way around). Generic `void *` pointers will convert to/from anything
just like in C, but the cv-qualifiers still have to be compatible.

As for `struct`/`union` types, you can create their respective pointers, but
the underlying type of the pointer must be compatible. Strings will only
convert to `char const *` or `char const[]`, or to arbitrary byte arrays
(the underlying type is a signed or unsigned or unspecified byte) in
initializer contexts (`cffi.new`).

Copy construction works in this context. You can create `struct`/`union` types
by passing `struct`/`union` cdata to them (or pointer), and a copy will happen.
It will also happen with other types (like scalars).

Function pointers are arbitrarily convertible as long as argument count is the
same. This is a relic of LuaJIT and may be changed to actual strict checks,
so don't count on it.

Table initializers are allowed in `cffi.new` but nowhere else.

There are some special considerations for the *pass rule*. When an argument
is expecting a reference, you can pass a base type `cdata` to it and its
address will be taken automatically. **This is not allowed in LuaJIT.**
However, this only applies to `cdata`, plain Lua values will not undergo
any intermediate conversions.

Arrays are allowed as output types under *pass rule*, but not any other rules.

### Special vararg conversions

These conversions apply when passing Lua values to C varargs. They are
different from the standard conversions.

| Input Lua type           | Output C type            |
|--------------------------|--------------------------|
| `number`                 | see note                 |
| `boolean`                | `bool`                   |
| `nil`                    | `NULL`                   |
| `userdata`               | `void *`                 |
| `lightuserdata`          | `void *`                 |
| `io.* file`              | `FILE *`                 |
| `string`                 | `char const *`           |
| `float` `cdata`          | `double`                 |
| array `cdata`            | element pointer          |
| function `cdata`         | function pointer         |
| `struct`/`union` `cdata` | `struct`/`union` pointer |
| any other `cdata`        | C type                   |

What this means in practice compared to standard argument passing is that as
required by C, `float`s are always promoted to `double`s, while `struct`s and
`union`s are never passed by value, but rather by address. Other conversions
should be trivial and obvious.

**Note**: if your Lua is configured with a floating point `number`, which it
generally is, numbers will be passed as `double`s in the general case, except
when configured to use `long double` and the `long double` is larger than
a `double` on your platform. With Lua 5.3, there is also support for integers,
which will be passed as matching integer C type. Therefore, be careful with
how you pass your values.

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
  Lua types, which means comparisons of `cdata` and `nil` (or other Lua
  values) will always be `false` no matter what. Incompatible `cdata`
  pointers can always be tested for address equality.

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

- Variables: the value undergoes conversion to its C type according to the
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
