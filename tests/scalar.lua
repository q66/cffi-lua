local ffi = require("cffi")

-- eval

local na = ffi.eval("0xFF")
local nb = ffi.eval("50ULL")
local nc = ffi.eval("50U")
local nd = ffi.eval("150LL")

assert(ffi.tonumber(na) == 255)
assert(ffi.tonumber(nb) == 50)
assert(ffi.tonumber(nc) == 50)
assert(ffi.tonumber(nd) == 150)

assert(ffi.typeof(na) == ffi.typeof("int"))
assert(ffi.typeof(nb) == ffi.typeof("unsigned long long"))
assert(ffi.typeof(nc) == ffi.typeof("unsigned int"))
assert(ffi.typeof(nd) == ffi.typeof("long long"))

-- basic arithmetic

local a = ffi.new("int", 150)
local b = ffi.new("short", 300)
local c = -ffi.cast("int", 150)

assert(ffi.tonumber(a + b + c) == 300)

-- pointer addition

local a = ffi.cast("int *", 0)
local b = a + 3
local c = 3 + a

assert(ffi.tonumber(ffi.cast("size_t", b)) == 3 * ffi.sizeof("int"))
assert(ffi.tonumber(ffi.cast("size_t", c)) == 3 * ffi.sizeof("int"))

-- pointer difference

local a = ffi.cast("int *", 12)
local b = ffi.cast("int *", 4)

assert((a - b) == ((12 - 4) / ffi.sizeof("int")))

-- arrays can be treated the same

local a = ffi.new("int[4]")
local b = a + 3

assert((b - a) == 3)

-- pow

assert(ffi.tonumber(ffi.cast("int", 2) ^ ffi.cast("int", 4)) == 16)

-- nullptr

assert(ffi.cast("int *", 0) == ffi.nullptr)
assert(ffi.cast("int *", 5) ~= ffi.nullptr)

-- equality

assert(ffi.cast("int", 150) == ffi.cast("int", 150))
assert(ffi.cast("int", 200) ~= ffi.cast("int", 150))
-- different from luajit
assert(ffi.cast("int", 150) ~= 150)
