local ffi = require("cffi")

if not ffi.abi("unionval") then
    skip_test()
end

ffi.cdef [[
    typedef union pass {
        int a;
        double b;
    } pass;

    typedef union pass2 {
        double a;
        double b;
    } pass2;

    pass test_union_val(pass a, pass b);
    pass2 test_union_val2(pass2 a);
]]

local L = require("testlib")

local a = ffi.new("pass", { b = 3.14 })
local b = ffi.new("pass", { b = 6.28 })

local c = L.test_union_val(a, b)

assert(c.a == 9)

-- test homogenous aggregates on certain ABIs

local a = ffi.new("pass2", { a = 3.14 })
local c = L.test_union_val2(a)

assert(c.b == 3.14)
