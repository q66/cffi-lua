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
