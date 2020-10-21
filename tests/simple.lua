local ffi = require("cffi")

ffi.cdef [[
    size_t test_strlen(char const *str);
    size_t test_strlen_void(void const *str) __asm__("test_strlen");
]]

local ret = ffi.tonumber(ffi.C.test_strlen("hello world"))
assert(ret == 11)

local ret = ffi.tonumber(ffi.C.test_strlen_void("hello world"))
assert(ret == 11)
