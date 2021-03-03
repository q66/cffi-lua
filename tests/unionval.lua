local ffi = require("cffi")

if not ffi.abi("unionval") then
    skip_test()
end

ffi.cdef [[
    typedef union pass {
        int a;
        double b;
    } pass;

    pass test_union_val(pass a, pass b);
]]

local L = require("testlib")

local a = ffi.new("pass")
local b = ffi.new("pass")

a.b = 3.14
b.b = 6.28

local c = L.test_union_val(a, b)

assert(c.a == 9)
