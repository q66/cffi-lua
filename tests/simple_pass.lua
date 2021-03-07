local ffi = require("cffi")
local L = require("testlib")

-- pattern to fill the memory with; long enough to cover every case
local pattern = string.char(0x3C, 0xF, 0xF0, 0xC3):rep(4)

local val_test = function(tname, fname)
    ffi.cdef(("struct { %s v; } test_%s(%s v);"):format(tname, fname, tname))
    local v = ffi.new(tname)
    local sz = ffi.sizeof(tname)
    local a = ffi.addressof(v)
    ffi.copy(ffi.cast("void *", a), pattern, sz)
    -- pass and check if it's still the same
    local r = L["test_" .. fname](v)
    assert(ffi.string(a, sz) == ffi.string(r, sz))
end

val_test("unsigned char", "uchar")
val_test("unsigned short", "ushort")
val_test("unsigned int", "uint")
val_test("unsigned long", "ulong")
val_test("unsigned long long", "ullong")
-- this case breaks on m68k with optimized builds; the result will be empty
-- it is not reproducible when returning the float type directly, and it is
-- also not reproducible with integer types
val_test("float", "float")
val_test("double", "double")

-- libffi seemingly has a bug where long double in a struct is not passed
-- correctly on x86_64 on Linux, should not be ours so don't trigger it
--
-- it's broken on m68k too
if ffi.os == "Windows" or (ffi.arch ~= "x64" and ffi.arch ~= "m68k") then
    val_test("long double", "ldouble")
end

ffi.cdef [[
    float test_raw_float(float v);
    char test_raw_char(char c);
    int test_raw_int(int v);
]]

assert(L.test_raw_float(15) == 15)
assert(L.test_raw_char(15) == 15)
assert(L.test_raw_int(15) == 15)
