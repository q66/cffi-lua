local ffi = require("cffi")

ffi.cdef [[
    typedef struct foo {
        int x;
        int y;
    } foo;
]]

local foo = ffi.metatype("foo", {
    __index = {
        named_ctor = function(x, y)
            return ffi.new("foo", x, y)
        end,

        sum = function(self)
            return self.x + self.y
        end
    },

    __len = function(self) return 1337 end,
    __unm = function(self) return -1337 end,
})

local x = foo(5, 10)
assert(x.x == 5)
assert(x.y == 10)
assert(x:sum() == 15)
assert(#x == 1337)
assert(-x == -1337)

local x = foo.named_ctor(500, 1000)
assert(x.x == 500)
assert(x.y == 1000)
assert(x:sum() == 1500)
