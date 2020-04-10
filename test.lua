-- TO BE REMOVED
-- only shows some functionality not yet covered by tests

local ffi = require("cffi")

ffi.cdef [[
    // just a typedef test
    typedef char *mbuf;
    typedef char const *cptr;

    int puts(char const *str);
    double strtod(const char *str, char **endptr);
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

print("# Scalar creation")
local v = ffi.eval("0x1234ABCD5678FFFF")
print("0x1234ABCD5678FFFF == " .. tostring(v))
print()

print("# Cdata arithmetic")
local a = ffi.new("int", 150)
local b = ffi.new("short", 300)
print("int(150) + short(300) == " .. tostring(a + b))
local a = ffi.cast("int *", 12)
local b = ffi.cast("int *", 4)
print("intptr(12) - intptr(4) == " .. (a - b))
print("int(2) ^ int(4) == " .. tostring(ffi.cast("int", 2) ^ ffi.cast("int", 4)))
print("-int(150) == " .. tostring(-ffi.cast("int", 150)))
print("intptr(0) == ffi.nullptr == " .. tostring(ffi.cast("int *", 0) == ffi.nullptr))
print("intptr(5) == ffi.nullptr == " .. tostring(ffi.cast("int *", 5) == ffi.nullptr))
print("int(150) == int(150) == " .. tostring(ffi.cast("int", 150) == ffi.cast("int", 150)))
print("int(200) == int(150) == " .. tostring(ffi.cast("int", 200) == ffi.cast("int", 150)))
print("int(150) == 150 == " .. tostring(ffi.cast("int", 150) == 150))
print()

print("## END TESTS ##")
