local ffi = require("cffi")

ffi.cdef [[
    // just some typedef tests...
    typedef char *mbuf;
    // weird infix syntax is a thing too
    const void *typedef cptr;

    double strtod(const char *str, char **endptr);
    int puts(char const *str);
    mbuf strdup(char const *x);
    void *memcpy(void *dest, cptr src, size_t num);
    void free(void *p);

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
ffi.C.free(foo)
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
local fp = ffi.new("int (*)(char const *s)", ffi.C.puts)
print("original puts == " .. tostring(ffi.C.puts))
print("pointer to puts == " .. tostring(fp))
print("calling with 'hello world'")
fp("hello world")
print()

print("# Callbacks")
local cb = ffi.new("void (*)(char const *x)", function(str)
    print("this is a callback, called from C...")
    print("passed data: " .. ffi.string(str) .. " (" .. tostring(str) .. ")")
end)
cb("hello world")
print("setting a different callback func...")
cb:set(function() print("a new callback func") end)
cb("hello world")
print("freeing callback via copy...")
local cb2 = ffi.new("void (*)(char const *x)", cb)
cb2:free()
print()

print("## END TESTS ##")
