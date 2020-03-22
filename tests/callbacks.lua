local ffi = require("cffi")

-- named args
local called = false
local cb = ffi.cast("void (*)(char const *arg1, int arg2)", function(arg1, arg2)
    assert(ffi.string(arg1) == "hello world")
    assert(ffi.tonumber(arg2) == 5)
    called = true
end)

cb("hello world", 5)
assert(called)

-- setting different callback
local called2 = false
cb:set(function() called2 = true end)

cb("foo", 10)
assert(called2)

cb:free()

-- unnamed args
local called3 = false
local cb2 = ffi.cast("void (*)(int, double)", function(a, b)
    assert(a == 5)
    assert(b == 3.14)
    called3 = true
end)

cb2(5, 3.14)
assert(called3)

cb2:free()
