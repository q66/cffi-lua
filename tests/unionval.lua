-- this tests exists to expose ABI issues with passing unions by value
-- for each test union, it is allocated locally on Lua side, filled with
-- a byte pattern, then passed by value to C; C simply returns it (again
-- by value) and then we do a byte comparison on both

local ffi = require("cffi")

if not ffi.abi("unionval") then
    skip_test()
end

local L = require("testlib")

-- pattern to fill the memory with; long enough to cover every case
local pattern = string.char(0x3C, 0xF, 0xF0, 0xC3):rep(32)

local union_test = function(uname, ubody)
    ffi.cdef(("typedef union %s { %s } %s; %s test_%s(%s v);"):format(
        uname, ubody, uname,
        uname, uname, uname
    ))
    local v = ffi.new(uname)
    local sz = ffi.sizeof(uname)
    ffi.copy(ffi.cast("void *", v), pattern, sz)
    -- pass and check if it's still the same
    local r = L["test_" .. uname](v)
    assert(ffi.string(v, sz) == ffi.string(r, sz))
end

math.randomseed(0)

union_test("u1", [[ signed char a; ]])
union_test("u2", [[ unsigned short a; ]])
union_test("u3", [[ int a; ]])
union_test("u4", [[ long long a; ]])
union_test("u5", [[ float a; ]])
union_test("u6", [[ double a; ]])
union_test("u7", [[ long double a; ]])

union_test("us1", [[ struct { signed char a; } x; ]])
union_test("us2", [[ struct { unsigned short a; } x; ]])
union_test("us3", [[ struct { int a; } x; ]])
union_test("us4", [[ struct { long long a; } x; ]])
union_test("us5", [[ struct { float a; } x; ]])
union_test("us6", [[ struct { double a; } x; ]])
union_test("us7", [[ struct { long double a; } x; ]])

union_test("ud1", [[ signed char a; int b; ]])
union_test("ud2", [[ unsigned short a; long b; ]])
union_test("ud3", [[ int a; signed char b; ]])
union_test("ud4", [[ long long a; float b; ]])
union_test("ud5", [[ float a; long long b; ]])
union_test("ud6", [[ double a; short b; ]])
union_test("ud7", [[ long double a; long b; ]])

union_test("ut1", [[ signed char a; struct { int x; float y; long z; } b; ]])
union_test("ut2", [[ unsigned short a; struct { char x; double y; long double z; } b; ]])
union_test("ut3", [[ int a; struct { int x; int y; int z; } b; ]])
union_test("ut4", [[ long long a; struct { short x; long y; float z; } b; ]])
union_test("ut5", [[ float a; struct { float x; float y; float z; float w; } b; ]])
union_test("ut6", [[ double a; struct { double x; int y; long long z; } b; ]])
union_test("ut7", [[ long double a; struct { long double x; long double y; long double z; } b; ]])

union_test("uh1", [[ struct { double x; double y; } a; struct { double x; double y; double z; } b; ]])
union_test("uh2", [[ double a; struct { double x; double y; } b; ]])
union_test("uh3", [[ struct { double x; struct { double y; double z; } w; } a; struct { double x; double y; double z; } b; ]])
union_test("uh4", [[ struct { double x; struct { double y; double z; } w; } a; struct { double x; } b; ]])

union_test("ui1", [[ struct { int x; int y; } a; struct { int x; int y; int z; } b; ]])
union_test("ui2", [[ int a; struct { int x; int y; } b; ]])
union_test("ui3", [[ struct { int x; struct { int y; int z; } w; } a; struct { int x; int y; int z; } b; ]])
union_test("ui4", [[ struct { int x; struct { int y; int z; } w; } a; struct { int x; } b; ]])
