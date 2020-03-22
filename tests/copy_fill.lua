local ffi = require("cffi")

local buf = ffi.new("char[256]")
assert(ffi.sizeof(buf) == 256)

ffi.copy(buf, "hello world")
assert(ffi.string(buf) == "hello world")

ffi.fill(buf, ffi.sizeof(buf))
assert(ffi.string(buf) == "")

ffi.fill(buf, 8, string.byte("A"))
assert(ffi.string(buf) == "AAAAAAAA")

ffi.fill(buf, 32)
ffi.copy(buf, "hello world", 5)
assert(ffi.string(buf) == "hello")
