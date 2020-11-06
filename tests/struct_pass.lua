local ffi = require("cffi")

ffi.cdef [[
    typedef struct pass {
        int a, b;
    } pass;

    pass test_struct_val(pass a, pass b);
]]

local L = require("testlib")

local a = ffi.new("pass", 5, 10)
local b = ffi.new("pass", 50, 100)

local c = L.test_struct_val(a, b)

assert(c.a == 55)
assert(c.b == 110)
