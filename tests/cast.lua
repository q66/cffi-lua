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

local x = ffi.new("int[2]", {5, 10})
local xp = ffi.cast("int *", x)

local tap = ffi.cast("void (*)(int p[2])", function(p)
    assert((p[0] == 5) and (p[1] == 10))
end)

tap(x)
tap(xp)

tap:free()
