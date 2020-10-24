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

-- scalar type dump by value

local a = ffi.new("uint32_t", 0x61626364)
local s = ffi.string(a, ffi.sizeof(a))
if ffi.abi("be") then
    assert(s == "abcd")
else
    assert(s == "dcba")
end

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
