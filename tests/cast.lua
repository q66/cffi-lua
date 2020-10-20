local ffi = require("cffi")

-- strings are convertible to char pointers

local foo = "hello world"
local foop = ffi.cast("const char *", foo)

assert(ffi.string(foop) == "hello world")

-- pointer<->number conversions

local up = ffi.cast("uintptr_t", foop)
local op = ffi.cast("const char *", up)

assert(ffi.string(op) == "hello world")
assert(op == foop)

-- passing pointers as arrays is ok

ffi.cdef [[
    int test_add_ptr(int p[2]);
]]

local x = ffi.new("int[2]", {5, 10})
local xp = ffi.cast("int *", x)

assert(ffi.C.test_add_ptr(x) == 15)
assert(ffi.C.test_add_ptr(xp) == 15)
