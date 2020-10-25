local ffi = require('cffi')

ffi.cdef [[
    const char test_string[];
    const char * const test_string_ptr;
    const int test_ints[3];
]]

assert(ffi.string(ffi.C.test_string) == "foobar")
assert(ffi.sizeof(ffi.C.test_string) == 0)

assert(ffi.C.test_string[0] == string.byte('f'))
assert(ffi.C.test_string[1] == string.byte('o'))
assert(ffi.C.test_string[2] == string.byte('o'))

assert(ffi.string(ffi.C.test_string_ptr) == "barbaz")

assert(ffi.C.test_ints[0] == 42)
assert(ffi.C.test_ints[1] == 43)
assert(ffi.C.test_ints[2] == 44)
