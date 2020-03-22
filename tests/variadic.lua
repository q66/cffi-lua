local ffi = require("cffi")

ffi.cdef [[
    int snprintf(char *buf, size_t n, char const *fmt, ...);
]]

local buf = ffi.new("char[256]")
local bufs = ffi.sizeof(buf)
assert(bufs == 256)

local ret = ffi.C.snprintf(ffi.cast("char *", buf), bufs, "%s", "test")
assert(ret == 4)
assert(ffi.string(buf) == "test")
local ret = ffi.C.snprintf(buf, bufs, "%d", ffi.new("int", 123456))
assert(ret == 6)
assert(ffi.string(buf) == "123456")

local ret = ffi.C.snprintf(buf, bufs, "%s %g", "hello", 3.14)
assert(ret == 10)
assert(ffi.string(buf) == "hello 3.14")
