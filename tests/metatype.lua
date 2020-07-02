local ffi = require("cffi")

ffi.cdef [[
    typedef struct foo {
        int x;
        int y;
    } foo;
]]

local foo = ffi.metatype("foo", {
    __index = {
        sum = function(self)
            return self.x + self.y
        end
    }
})

local x = foo(5, 10)
assert(x.x == 5)
assert(x.y == 10)
assert(x:sum() == 15)
