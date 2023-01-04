local ffi = require("cffi")

ffi.cdef [[
    struct flex {
        int x;
        double y[];
    };

    struct vlaflex {
        int x;
        double y[?];
    };

    struct buf {
        uint32_t x;
        uint32_t y;
        uint8_t buf[16];
    };

    struct sinit {
        int x;
        float y;
        double z;
    };

    struct sstr {
        int x;
    };

    struct dsinit {
        struct sstr a;
        struct sstr b;
    };

    union uinit {
        char const *s;
        size_t a;
    };

    union uinit2 {
        int a;
        double b;
    };
]]

local x

x = ffi.new("int[3]", 5)
assert(x[0] == 5)
assert(x[1] == 5)
assert(x[2] == 5)
assert(ffi.sizeof(x) == 3 * ffi.sizeof("int"))

x = ffi.new("int[?]", 3, 5)
assert(x[0] == 5)
assert(x[1] == 0)
assert(x[2] == 0)
assert(ffi.sizeof(x) == 3 * ffi.sizeof("int"))

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

x = ffi.new("int[3]", 5, 10, 15)
assert(x[0] == 5)
assert(x[1] == 10)
assert(x[2] == 15)
assert(ffi.sizeof(x) == 3 * ffi.sizeof("int"))

x = ffi.new("int[?]", 3, 5, 10, 15)
assert(x[0] == 5)
assert(x[1] == 10)
assert(x[2] == 15)
assert(ffi.sizeof(x) == 3 * ffi.sizeof("int"))

x = ffi.new("struct sinit", { 5, 3.14, 6.28 })
assert(x.x == 5)
assert(x.y == ffi.tonumber(ffi.new("float", 3.14)))
assert(x.z == 6.28)

x = ffi.new("struct sinit", 5, 3.14, 6.28)
assert(x.x == 5)
assert(x.y == ffi.tonumber(ffi.new("float", 3.14)))
assert(x.z == 6.28)

x = ffi.new("struct sinit", { x = 5, y = 3.14, z = 6.28 })
assert(x.x == 5)
assert(x.y == ffi.tonumber(ffi.new("float", 3.14)))
assert(x.z == 6.28)

x = ffi.new("struct dsinit", {5}, {10})
assert(x.a.x == 5)
assert(x.b.x == 10)

x = ffi.new("struct dsinit", {{5}, {10}})
assert(x.a.x == 5)
assert(x.b.x == 10)

x = ffi.new("struct flex", 2, { 5, 10, 15 })
assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)

x = ffi.new("struct flex", 2, { x = 5, y = { 10, 15 } })
assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)

x = ffi.new("struct vlaflex", 2, { 5, 10, 15 })
assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)

x = ffi.new("struct vlaflex", 2, { x = 5, y = { 10, 15 } })
assert(x.x == 5)
assert(x.y[0] == 10)
assert(x.y[1] == 15)

x = ffi.new("struct buf", { x = 5, y = 10 });
assert(x.x == 5)
assert(x.y == 10)
ffi.copy(x.buf, "hello world")
assert(x.buf[0] == string.byte("h"))
assert(x.buf[1] == string.byte("e"))
assert(ffi.string(x.buf) == "hello world")

local str = "hello world"
x = ffi.new("union uinit", { str })
assert(ffi.string(x.s) == "hello world")
assert(x.s == ffi.cast("void *", x.a))
assert(x.s ~= x.a)

x = ffi.new("union uinit", { s = str })
assert(ffi.string(x.s) == "hello world")
assert(x.s == ffi.cast("void *", x.a))
assert(x.s ~= x.a)

-- test if union initialization by name works
x = ffi.new("union uinit2", { a = 5 })
assert(x.a == 5)

-- test union init of first field
x = ffi.new("union uinit2", 5)
assert(x.a == 5)

-- should be properly truncated
x = ffi.new("union uinit2", { a = 3.14 })
assert(x.a == 3)

-- test initialization of second field
x = ffi.new("union uinit2", { b = 3.14 })
assert(x.b == 3.14)
