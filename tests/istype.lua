local ffi = require('cffi')

ffi.cdef [[
    struct foo {
        double v;
    };

    struct test {
        struct foo v;
        double arr[3];
    };
]]

local x = ffi.new('struct test')

-- struct fields
assert(ffi.istype("double[3]", x.arr))
assert(ffi.istype("double (&)[3]", x.arr))
assert(not ffi.istype("int[3]", x.arr))
assert(ffi.istype("struct foo", x.v))
assert(ffi.istype("struct foo &", x.v))
assert(not ffi.istype("struct foo[]", x.v))

-- references must be ignored
assert(ffi.istype("double [3]", ffi.typeof("double (&)[3]")))
assert(ffi.istype("double (&)[3]", ffi.typeof("double [3]")))
assert(ffi.istype("int", ffi.typeof("int &")))
assert(ffi.istype("int &", ffi.typeof("int")))
assert(ffi.istype("float &", ffi.typeof("float")))

-- non-equal types
assert(not ffi.istype("int", ffi.typeof("float")))
assert(not ffi.istype("int &", ffi.typeof("float")))
assert(not ffi.istype("int &", ffi.typeof("float &")))

-- second argument must be a cval
assert(not ffi.istype("int", "int"))
assert(not ffi.istype("int", true))

-- alternative syntaxes
assert(ffi.istype("long", ffi.typeof("long int")))
assert(ffi.istype("unsigned long", ffi.typeof("unsigned long int")))
assert(ffi.istype("long int", ffi.typeof("long")))
assert(ffi.istype("unsigned long int", ffi.typeof("unsigned long")))
assert(ffi.istype("short", ffi.typeof("short int")))
assert(ffi.istype("unsigned short", ffi.typeof("unsigned short int")))
assert(ffi.istype("short int", ffi.typeof("short")))
assert(ffi.istype("unsigned short int", ffi.typeof("unsigned short")))

-- types in typeof must be terminated
local ret, msg = pcall(ffi.typeof, "long int bla")
assert(not ret)
assert(msg == "'<eof>' expected near '<name>'")

local ret, msg = pcall(ffi.typeof, "long int int")
assert(not ret)
assert(msg == "'<eof>' expected near 'int'")
