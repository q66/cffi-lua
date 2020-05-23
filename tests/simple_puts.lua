local ffi = require("cffi")

ffi.cdef [[
    int __stdcall test_puts(char const *str);
]]

local ret = ffi.C.test_puts("hello world")
assert(ret >= 0)
