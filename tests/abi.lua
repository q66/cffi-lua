-- ffi.os, ffi.arch is not tested as that will vary by platform
-- same with other ffi.abi parameters, etc

local ffi = require("cffi")

if ffi.abi("be") then
    assert(not ffi.abi("le"))
else
    assert(ffi.abi("le"))
    assert(not ffi.abi("be"))
end

if ffi.abi("64bit") then
    assert(ffi.sizeof("void *") == 8)
elseif ffi.abi("32bit") then
    assert(ffi.sizeof("void *") == 4)
else
    skip_test()
end

assert(ffi.sizeof("char") == 1)
assert(ffi.sizeof("short") == 2)

ffi.cdef [[
    union foo {
        struct { uint8_t a; uint8_t b; };
        uint16_t v;
    };
]]

local x = ffi.new("union foo")
x.a = 0xAA
x.b = 0xFF

if ffi.abi("be") then
    assert(x.v == 0xAAFF)
else
    assert(x.v == 0xFFAA)
end
