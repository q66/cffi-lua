local ffi = require("cffi")

-- parameterized struct

local t1_t = ffi.typeof("int")
local t2_t = ffi.typeof("char const *")

ffi.cdef([[
    typedef struct $ {
        $ $;
        $ $;
    } $;
]], "pstruct", t1_t, "test1", t2_t, "test2", "pstruct")

local x = ffi.new("pstruct")
x.test1 = 1337
local t2 = "test string"
x.test2 = t2

assert(ffi.tonumber(x.test1) == 1337)
assert(ffi.string(x.test2) == "test string")

assert(ffi.sizeof("pstruct") == ffi.sizeof("struct { int x; void *y; }"))

-- parameterized enum

ffi.cdef([[
    enum {
        $ = $, $
    };
]], "FOO", 1337, "BAR")

assert(ffi.C.FOO == 1337)
assert(ffi.C.BAR == 1338)
