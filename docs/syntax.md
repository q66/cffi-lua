# Supported C syntax

The project supports a subset of the C11 language, plus some extensions. There
are still extensions as well as actual official language features missing. This
document does not aim to provide any sort of spec or formal grammar, as all
the features implemented are just C features; it's more or less a set of
guidelines on what kind of syntax is allowed.

This will also not cover things such as parameterized types, which are not
a part of the core syntax per se, but rather specific to certain APIs.

This document will get expanded as more syntax is implemented.

## Declarations

### Functions

#### Basic functions

**Syntax:**

```
T function_name();
T function_name(void);
T function_name(A1, A2, A3);
T function_name(A1, A2, A3, ...);
```

Basic function declarations are supported, including variadic function
declarations.

**Note:** Declaring a function with an empty argument list follows C++
semantics! Therefore, `T foo();` is equivalent to `T foo(void);`.

**Extension:** Unnamed arguments are supported, therefore `void foo(int x)` and
simple `void foo(int)` are identical.

You can also write:

```
T (function_name(...)); // function returning T
T (*function_name(...)); // function returning T *
```

This is a part of standard C rules on how types can be parenthesized, in
this case leaving out the parenthesis doesn't change anything. Note how
things change when you return function pointers though:

#### Returning function pointers

**Syntax:**

```
// T == return type of function pointer
// A1 == arglist of the function
// A2 == arglist of the function pointer
T (*function_name(A1))(A2);
```

This is the same as in C. It can be nested as well:

```
T (*(*function_name(A1))(A2))(A3);
```

Where `A1` is the argument list of the function, `A2` is the argument list
of the function pointer it returns, and `A3` is the argument list of the
function pointer the function pointer returns, with `T` being the return
type of the last returned function pointer.

### Variables

**Syntax:**

```
T var;
extern T var;
T (var);
extern T (var);
```

These syntaxes mean the same thing.

#### Pointer variables

**Syntax:**

```
T *var;
T (*var);
```

These syntaxes mean the same thing. This extends to how pointers bind to
their underlying types, like in function pointers.

#### Array variables

**Syntax:**

```
T var[N];
T *var[N]; // array of pointers to T
T (*var)[N]; // pointer to array of T
T *(*var)[N]; // pointer to array of pointers to T
```

Unbounded array declarations are allowed, but you will not be able to get
their size or instantiate their type.

```
T var[];
```

All array types effectively decay to pointers.

See the section about types below for more array-related details.

### Typedefs

**Syntax:**

```
typedef FROM TO; // typedef from normal type
typedef T (*TO)(...); // typedef from function pointer type
```

These follow standard C rules.

**Note:** Unlike in C, `typedef` is currently not treated as storage class.
Therefore, writing `int typedef foo;` is currently not possible.

**Note:** Using `typedef` will not create a type, but rather just an internal
alias. Referring to something via `typedef` will always expand to the underlying
type during usage, regardless of the number of alias levels.

### Structs and unions

**Syntax:**

```
struct foo {
    ...
};

union bar {
    ...
};
```

You can couple `struct` with `typedef` as usual. Unnamed `struct` and `union`
is also possible. C rules are followed, so you can't refer to a non-`typedef`
`struct` simply by name, you need to use the full `struct name` form.

**Extension:** You can specify a transparent `struct` inside a `struct`, and
its members will be accessible the same as other members.

```
struct foo {
    int x;
    struct {
        int y;
    };
    int z;
};
```

Transparent `union` is also possible:

```
struct foo {
    union {
        ...
    };
};
```

Same with the other way around.

Opaque `struct` and `union` is also possible:

```
struct foo;
```

**Note:** Unnamed `struct`s or `union`s or `enum`s will get an implicit name
assigned internally. You can not retrieve them by name directly afterwards,
however you can still get handles to them via `cffi.typeof`, and use them
together with e.g. parameterized types.

Different `struct`s (and `union`s) are never equal, even if their members are.
Therefore, creating two unnamed `struct`s will always result in distinct types.

### Enums

**Syntax:**

```
enum foo {
    A, B, C, ...
};
```

Explicitly setting values is possible:

```
enum foo {
    A = 5, B, C, D = 1 << 10, ...
};
```

The following members will increment their value by one.

**Note:** Currently all underlying type of all `enum` declarations is `int`.
Therefore, `enum`s with large values are not yet supported.

All `enum` members are exported into global scope, so you can access their
values via `ffi.C.<member_name>`.

**Extension:** Opaque `enum`s are supported.

## Types

All core integer types are supported: `bool`, `_Bool`, `char`, `signed char`,
`unsigned char`, `short`, `unsigned short`, `int`, `unsigned int`, `long`,
`unsigned long`, `long long`, `unsigned long long`.

All core floating point types are supported: `float`, `double`, `long double`.

The `bool` type is a distinct type, like in C++. `_Bool` is an alias to it.
**This follows C++ semantics.**

The `char` type is a distinct type with an unknown signedness (platform
specific). The `signed char` and `unsigned char` types are distinct from
plain `char`.

Like in C, `unsigned` used alone is `unsigned int`, same with `signed` and
`signed int`.

**Note:** The `signed` and `unsigned` keywords are only allowed before the
type name, i.e. no `int unsigned x;` like in C.

**The following builtin types are also defined:** `va_list`, `__builtin_va_list`,
`__gnuc_va_list` - these are all aliases to the same `va_list` type

**The following aliases are implicitly defined:**

- From `stddef.h`: `size_t`, `ptrdiff_t`, `wchar_t`
- From `stdint.h`: `int8_t`, `int16_t`, `int32_t`, `int64_t`, `uint8_t`,
  `uint16_t`, `uint32_t`, `uint64_t`
- From `uchar.h`: `char16_t`, `char32_t`
- From `sys/types.h`: `ssize_t`, `time_t`

**The following types are not yet supported:** `complex` / `_Complex` variants

### Array types

Variable length arrays are supported with a special syntax `T[?]`. This is
generally treated like an unbounded array within relevant type contexts (as
it has no size by itself), but for FFI `ctype`s and instantiations, it is
treated specially. In most cases, you will be using VLAs when instantiating
types, e.g. via `cffi.typeof`. However, it is also possible to e.g. `typedef`
a VLA and instantiate it later through its alias.

During instantiation of VLAs, you will be required to provide a size, and
the resulting `cdata` will have a runtime size, which you can retrieve via
`cffi.sizeof` like with a normal static array.

### Struct/union types

Just like in C, you can use anonymous or named `struct`s or `union`s as types,
not just type declarations.

### C++ references

As an extension, the FFI supports C++ style references (`T &`). They are
implemented as pointers, but have different semantics.

It is not possible to declare a pointer to a reference, nor it is possible to
declarare a reference to a reference. Function references are possible with
the usual syntax `T (&foo)(...)`, as are references to anything else.

In most cases, references will automatically decay to their underlying values
like in C++. For example, when using reference `cdata` in `cffi.cast` or
`cffi.new`, they are always dereferenced and treated like their underlying
type, same with passing them to function. If the target type is a reference
type, a new reference to the underlying value will be taken. Internally, this
may or may not happen; the FFI can sometimes choose to retain the reference
as with a valid value, this is effectively the same.

## Expressions

The FFI features a complete expression parser. The following expressions are
supported:

- Binary expressions `A op B` - these support the usual set of binary operators,
  `+ - * / % == != > < >= <= && || & | ^ << >>`
- Unary expressions `op A` - with operators `+ - ! ~`
- Ternary operator `C ? T : F`
- Integer literals
- The `true` and `false` boolean literals
- The `sizeof` expression
- The `alignof` expression (including `__alignof__` syntax)
- Parenthesized expression `(E)`

All standard C operator precedences are followed.

**The following expressions are not yet supported:**

- Name references to constants (e.g. `enum` constants)
- `sizeof` with an expression (only type is supported)
- String literals
- Character literals
- Floating point literals
- Complex number literals

Integer literals support decimal, hexadecimal, octal and binary (**C++ ext**)
versions, including the standard suffixes for signedness and length.
