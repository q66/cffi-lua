local ffi = require("cffi")

ffi.cdef [[
    // just a typedef test
    typedef char *mbuf;
    typedef char const *cptr;

    double strtod(const char *str, char **endptr);
    int puts(char const *str);
    mbuf strdup(char const *x);
    void *memcpy(void *dest, cptr src, size_t num);
    void free(void *p);
    int printf(const char *fmt, ...);

    enum test {
        FIRST = 1, SECOND, THIRD, FOURTH,

        SOME_FLAG = 1 << 4,
        SOME_SIZE = sizeof(void *),
        SOME_ALIGN = alignof(long double)
    };
]]

print("## BEGIN TESTS ##")
print()

print("# system info")
print("os: " .. ffi.os)
print("arch: " .. ffi.arch)
print("endian: " .. (ffi.abi("le") and "le" or "be"))
print("bits: " .. (ffi.abi("64bit") and "64" or "32"))
print()

print("# main library namespace")
print("C namespace: " .. tostring(ffi.C))
print()

print("# library load")
local lualib = "liblua" .. _VERSION:sub(5) .. ".so"
print("loading self: " .. lualib)
local llib = ffi.load(lualib)
print("loaded: " .. tostring(llib))
ffi.cdef [[void *luaL_newstate(void);]]
print("luaL_newstate: " .. tostring(llib.luaL_newstate))
print()

print("# type of a function")
print("ffi.C.strtod == " .. tostring(ffi.typeof(ffi.C.strtod)))
print()

print("# string printing")
local ret = ffi.C.puts("hello world")
print("received from puts: " .. ret)
print()

print("# strtod")
local test = ffi.C.strtod(tostring(22 / 7), nil)
print("strtod(tostring(22 / 7)) == " .. tostring(test))
print()

print("# invalid conversion")
print(pcall(ffi.C.strtod, 150, nil))
print()

print("# conversion with endptr")
local endp = ffi.new("char *")
test = ffi.C.strtod("150foo", ffi.addressof(endp))
print("strtod(\"150foo\", endp) == " .. tostring(test))
print("endptr: " .. ffi.string(endp))
print()

print("# strdup")
local input = "some random string"
print("input: " .. input)

local foo = ffi.C.strdup(input)
print("dup'd: " .. tostring(foo))

print("copying '<redacted>' into the string...")
--ffi.C.memcpy(foo, "<redacted>", 10);
ffi.copy(foo, "<redacted>", 10)
print("second character: " .. string.char(foo[1]))
print("setting second character to R...")
foo[1] = string.byte("R")
local bar = ffi.string(foo)
print("converted back to string: " .. bar)
print("setting finalizer...")
ffi.gc(foo, function(x)
    print("finalizer for " .. tostring(x))
    ffi.C.free(x)
end)
print()

print("# varargs (printf)")
ffi.C.printf("hello world %g %p \"%s\"\n", 22 / 7, nil, foo)
print()

print("# type of a ptr")
local pt = ffi.typeof(foo);
print("typeof(foo) == " .. tostring(pt))
print("instantiated via ctor == " .. tostring(pt()))
print("instantiated via string == " .. tostring(ffi.new("mbuf")))
print("instantiated via ffi.new(ct) == " .. tostring(ffi.new(pt)))
print("size == " .. ffi.sizeof(pt))
print("alignment == " .. ffi.alignof(pt))
print()

print("# enum constants")
print("ffi.C.THIRD = " .. ffi.C.THIRD)
print("ffi.C.SOME_FLAG = " .. ffi.C.SOME_FLAG)
print("ffi.C:SOME_SIZE = " .. ffi.C.SOME_SIZE)
print("ffi.C.SOME_ALIGN = " .. ffi.C.SOME_ALIGN)
print()

print("# scalar cdata")
local scd = ffi.new("int", 10);
print("int(10) == " .. tostring(scd))
print()

print("# Function pointers")
local fp = ffi.cast("int (*)(char const *s)", ffi.C.puts)
print("original puts == " .. tostring(ffi.C.puts))
print("pointer to puts == " .. tostring(fp))
print("calling with 'hello world'")
fp("hello world")
print()

print("# Callbacks")
local cb = ffi.cast("void (*)(char const *x)", function(str)
    print("this is a callback, called from C...")
    print("passed data: " .. ffi.string(str) .. " (" .. tostring(str) .. ")")
end)
cb("hello world")
print("setting a different callback func...")
cb:set(function() print("a new callback func") end)
cb("hello world")
print("freeing callback via copy...")
local cb2 = ffi.cast("void (*)(char const *x)", cb)
cb2:free()
print("attempt to call now invalid callback:")
print(pcall(cb, "hello world"))
print()

print("# Scalar creation")
local v = ffi.eval("0x1234ABCD5678FFFF")
print("0x1234ABCD5678FFFF == " .. tostring(v))
print()

print("# C++ references")
print("making cdata of character 'c'...")
local cd = ffi.new("char", string.byte("c"))
print("making reference to the cdata...")
local ref = ffi.ref(cd)
print("setting character to 'd' via cdata...")
ref[0] = string.byte("d")
print("new value: " .. string.char(ffi.tonumber(cd)))
print("making mutable string 'foo'...")
local foo = ffi.C.strdup("foo")
print("making reference to the string...")
ref = ffi.new("char &", foo)
print("setting first character to 'b' via ref...")
ref[0] = string.byte("b")
print("new string: " .. ffi.string(foo))
print("type of string: " .. ffi.type(foo))
ffi.C.free(foo)
print()

ffi.cdef [[
    /* include some paddings to make sure we can have large structs */
    struct foo {
        int x;
        size_t pad1;
        size_t pad2;
        struct {
            char y;
            size_t pad3;
            size_t pad4;
            short z;
        };
        size_t pad5;
        size_t pad6;
        char const *w;
    };
]]

print("# Structs")
x = ffi.new("struct foo")
print("new struct: " .. tostring(x))
local s = "hello"
x.x = 150
x.y = 30
x.z = 25
x.w = s
print("members: ")
print("x: " .. x.x)
print("y: " .. x.y)
print("z: " .. x.z)
print("w: " .. ffi.string(x.w))
print("offset of z: " .. ffi.offsetof("struct foo", "z"))
print()

print("# Arrays")
x = ffi.new("int[3]")
print("arr: " .. tostring(x) .. " (size: " .. ffi.sizeof(x) .. ")")
x[0] = 5
x[1] = 10
x[2] = 15
for i = 0, 2 do
    print("arr[" .. i .. "] = " .. ffi.tonumber(x[i]))
end
print()

print("# Variable length arrays")
x = ffi.new("int[?]", 3)
print("arr: " .. tostring(x) .. " (size: " .. ffi.sizeof(x) .. ")")
x[0] = 5
x[1] = 10
x[2] = 15
for i = 0, 2 do
    print("arr[" .. i .. "] = " .. ffi.tonumber(x[i]))
end
print()

print("# Arrays initialized with a value")
x = ffi.new("int[3]", 123);
print("regular: " .. x[0] .. ", " .. x[1] .. ", " .. x[2])
x = ffi.new("int[?]", 3, 123);
print("VLA: " .. x[0] .. ", " .. x[1] .. ", " .. x[2])
print()

print("# Flexible array members")
ffi.cdef [[
    struct flex {
        int x;
        double y[];
    };
]]
x = ffi.new("struct flex", 3);
x.x = 5
x.y[0] = 10
x.y[1] = 15
x.y[2] = 20
print("2nd member of flexible struct's array part: " .. x.y[1])
print()

print("# Parameterized types")

print("testing parameterized struct...")

local t1_t = ffi.typeof("int")
local t2_t = ffi.typeof("char const *")

ffi.cdef([[
    typedef struct $ {
        $ $;
        $ $;
    } $;
]], "pstruct", t1_t, "test1", t2_t, "test2", "pstruct")

print("defined a struct 'pstruct' with members 'test1' and 'test2'...")
x = ffi.new("pstruct")
print("created: " .. tostring(x))
x.test1 = 1337
local t2 = "test2 string"
x.test2 = t2
print("x.test1: " .. ffi.tonumber(x.test1))
print("x.test2: " .. ffi.string(x.test2))

print("testing parameterized enum...")

ffi.cdef([[
    enum {
        $ = $, $
    };
]], "FOO", 1337, "BAR")

print("created anonymous enum with members FOO and BAR...")
print("FOO: " .. ffi.tonumber(ffi.C.FOO))
print("BAR: " .. ffi.tonumber(ffi.C.BAR))
print()

print("# Table initializers")

ffi.cdef [[
    struct sinit {
        int x;
        float y;
        double z;
    };
]]

x = ffi.new("int[3]", { 5, 10, 15 })
print("static array: " .. x[0] .. ", " .. x[1] .. ", " .. x[2])
x = ffi.new("int[?]", 3, { 5, 10, 15 })
print("VLA: " .. x[0] .. ", " .. x[1] .. ", " .. x[2])
x = ffi.new("struct sinit", { 5, 3.14, 6.28 })
print("struct: " .. x.x .. ", " .. x.y .. ", " .. x.z)
x = ffi.new("struct sinit", { x = 5, y = 3.14, z = 6.28 })
print("struct with names: " .. x.x .. ", " .. x.y .. ", " .. x.z)
x = ffi.new("struct flex", 2, { 5, 10, 15 })
print("flex struct: " .. x.x .. ", " .. x.y[0] .. ", " .. x.y[1])
x = ffi.new("struct flex", 2, { x = 5, y = { 10, 15 } })
print("flex struct with names: " .. x.x .. ", " .. x.y[0] .. ", " .. x.y[1])
print()

print("# Unions")

ffi.cdef [[
    union utest {
        struct {
            int x;
            int y;
        };
        long z;
    };
]]

x = ffi.new("union utest")
x.x = 5
x.y = 10
print("union: " .. x.x .. ", " .. x.y .. ", " .. tostring(x.z))

ffi.cdef [[
    union utestb {
        char const *s;
        size_t a;
    };
]]

local str = "hello world"
x = ffi.new("union utestb", { str })
print("table initialized union: " .. tostring(x.s) .. ", " .. tostring(x.a))
x = ffi.new("union utestb", { s = str })
print("table initialized union (via name): " .. tostring(x.s) .. ", " .. tostring(x.a))
print()

print("# Cdata arithmetic")
local a = ffi.new("int", 150)
local b = ffi.new("short", 300)
print("int(150) + short(300) == " .. tostring(a + b))
print()

print("## END TESTS ##")
