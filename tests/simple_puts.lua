local ffi = require("cffi")

ffi.cdef [[
    int puts(char const *str);
]]

local ret = ffi.C.puts("hello world")
assert(ret >= 0)
