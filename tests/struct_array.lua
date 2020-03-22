local ffi = require("cffi")

ffi.cdef [[
    struct foo {
        int x;
        size_t pad1;
        size_t pad2;
        struct {
            char y;
            size_t pad3;
            size_t pad4;
            short z;
        };
        size_t pad5;
        size_t pad6;
        char const *w;
    };

    struct flex {
        int x;
        double y[];
    };

    union bar {
        struct {
            unsigned char x;
            unsigned char y;
        };
        unsigned short z;
    };
]]

-- struct

local x = ffi.new("struct foo")

local s = "hello"
x.x = 150
x.y = 30
x.z = 25
x.w = s

assert(x.x == 150)
assert(x.y == 30)
assert(x.z == 25)
assert(ffi.string(x.w) == "hello")

local ox = ffi.offsetof("struct foo", "x")
local oy = ffi.offsetof("struct foo", "y")
local oz = ffi.offsetof("struct foo", "z")
local ow = ffi.offsetof("struct foo", "w")

assert(ox == 0)
assert(oy > ox)
assert(oz > oy)
assert(ow > oz)

-- simple array

local x = ffi.new("int[3]")
assert(ffi.sizeof(x) == ffi.sizeof("int") * 3)

assert(x[0] == 0)
assert(x[1] == 0)
assert(x[2] == 0)

x[0] = 5
x[1] = 10
x[2] = 15

assert(x[0] == 5)
assert(x[1] == 10)
assert(x[2] == 15)

-- array with initializer

local x = ffi.new("int[3]", 5)
assert(ffi.sizeof(x) == ffi.sizeof("int") * 3)

assert(x[0] == 5)
assert(x[1] == 5)
assert(x[2] == 5)

-- flexible array members

local x = ffi.new("struct flex", 3);
x.x = 5
x.y[0] = 10
x.y[1] = 15
x.y[2] = 20

assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)
assert(x.y[2] == 20)

-- union

local x = ffi.new("union bar")
x.x = 5
x.y = 10

assert(x.x == 5)
assert(x.y == 10)
if ffi.abi("le") then
    assert(x.z == 0xA05)
else
    assert(x.z == 0x50A)
end
