local ffi = require("cffi")

ffi.cdef [[
    typedef struct foo {
        int x;
    } foo;
]]

local closed_count = 0

local foo = ffi.metatype("foo", {
    __close = function()
        closed_count = closed_count + 1
    end
})

local x <close> = foo(5)
assert(closed_count == 0)

do
    local x <close> = foo(5)
end
assert(closed_count == 1)

do
    local x <close> = foo(5)
    local y <close> = foo(5)
    do
        local z <close> = foo(5)
    end
    assert(closed_count == 2)
end

assert(closed_count == 4)
