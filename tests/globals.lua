local ffi = require('cffi')

ffi.cdef [[
    extern char const test_string[];
    extern int const test_ints[3];
]]

assert(ffi.C.test_string[0] == string.byte('f'))
assert(ffi.C.test_string[1] == string.byte('o'))
assert(ffi.C.test_string[2] == string.byte('o'))

assert(ffi.C.test_ints[0] == 42)
assert(ffi.C.test_ints[1] == 43)
assert(ffi.C.test_ints[2] == 44)

-- must be references
assert(tostring(ffi.typeof(ffi.C.test_string)) == "ctype<char const(&)[]>")
assert(tostring(ffi.typeof(ffi.C.test_ints)) == "ctype<int const(&)[3]>")