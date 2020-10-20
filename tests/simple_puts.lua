local ffi = require("cffi")

ffi.cdef [[
    int test_puts(char const *str);
    int test_puts_void(void const *str) __asm__("test_puts");
]]

local ret = ffi.C.test_puts("hello world")
assert(ret >= 0)

local ret = ffi.C.test_puts_void("hello world")
assert(ret >= 0)
