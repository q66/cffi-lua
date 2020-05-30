local ffi = require("cffi")

ffi.cdef [[
    typedef enum {
        A = 1 << 0,
        B = 1 << 1,
        AB = A | B,
        C = 1 << 2,
        D = 1 << 3,
        E = 1 << 4,
        F = 1 << 5,
        G = 1 << 6,
        AC = A | C,
        ABC = AB | C,
        ALL = ABC | D | E | F | G
    } Test;
]]

assert(ffi.C.A == 1)
assert(ffi.C.B == 2)
assert(ffi.C.AB == 3)
assert(ffi.C.C == 4)
assert(ffi.C.D == 8)
assert(ffi.C.E == 16)
assert(ffi.C.F == 32)
assert(ffi.C.G == 64)
assert(ffi.C.AC == 5)
assert(ffi.C.ABC == 7)
assert(ffi.C.ALL == 127)
