local ffi = require("cffi")

-- simple cases

local s = "hello world"

assert(ffi.string(s) == s)
assert(ffi.string(ffi.cast("char const *", s)) == s)

-- byte array dump + pointer to it

local a = ffi.new("uint8_t [3]", {0x61, 0x62, 0x63})
local p = ffi.cast("const uint8_t *", a)

assert(ffi.string(a, ffi.sizeof(a)) == "abc")
assert(ffi.string(p, 3) == "abc")

-- scalar values are not dumpable

local a = ffi.new("uint32_t", 0x61626364)

assert(not pcall(ffi.string, a, ffi.sizeof(a)))

-- struct type dump by value

ffi.cdef [[
    struct foo {
        uint8_t a;
        uint8_t b;
        uint8_t c;
    };
]]

local s = ffi.new("struct foo", 0x61, 0x62, 0x63)
local p =ffi.cast("struct foo const *", s)

assert(ffi.string(s, ffi.sizeof(s)) == "abc")
assert(ffi.string(p, ffi.sizeof(s)) == "abc")

-- flexible arrays should be treated like strings

ffi.cdef [[
    struct bar {
        uint8_t x;
        char s[];
    };
]]

local b = ffi.new("char [5]", 0x10, 0x61, 0x62, 0x63, 0x00)
local p = ffi.cast("struct bar const *", b)

assert(p.x == 16)
assert(ffi.string(p.s, 2) == "ab")
assert(ffi.string(p.s) == "abc")
