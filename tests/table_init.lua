local ffi = require("cffi")

ffi.cdef [[
    struct flex {
        int x;
        double y[];
    };

    struct sinit {
        int x;
        float y;
        double z;
    };
]]

local x

x = ffi.new("int[3]", { 5, 10, 15 })
assert(x[0] == 5)
assert(x[1] == 10)
assert(x[2] == 15)
assert(ffi.sizeof(x) == 3 * ffi.sizeof("int"))

x = ffi.new("int[?]", 3, { 5, 10, 15 })
assert(x[0] == 5)
assert(x[1] == 10)
assert(x[2] == 15)
assert(ffi.sizeof(x) == 3 * ffi.sizeof("int"))

x = ffi.new("struct sinit", { 5, 3.14, 6.28 })
assert(x.x == 5)
assert(x.y == ffi.tonumber(ffi.new("float", 3.14)))
assert(x.z == 6.28)

x = ffi.new("struct sinit", { x = 5, y = 3.14, z = 6.28 })
assert(x.x == 5)
assert(x.y == ffi.tonumber(ffi.new("float", 3.14)))
assert(x.z == 6.28)

x = ffi.new("struct flex", 2, { 5, 10, 15 })
assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)

x = ffi.new("struct flex", 2, { x = 5, y = { 10, 15 } })
assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)
