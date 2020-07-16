local ffi = require("cffi")

-- it's okay to redefine funcs (and extern vars)
-- the first definition always stays
-- type matching is not checked, as luajit doesn't either
ffi.cdef [[
    void *malloc(size_t n);
    int malloc(float n);

    void free(void *p);
    void free(int p);
]]

local p = ffi.C.malloc(4)
assert(ffi.istype("void *", p))

ffi.cdef [[
    // try an empty typedef while at it, struct will get registered
    typedef struct foo { int x; };
]]

-- redefining structs etc. is invalid
local ret, msg = pcall(ffi.cdef, [[
    struct foo { int x; };
]])

assert(not ret)
assert(msg == "input:1: 'struct foo' redefined")

-- typedef redefinitions are also okay, first one applies
ffi.cdef [[
    typedef struct foo foo;
    typedef struct bar foo;
]]

assert(tostring(ffi.typeof("foo")) == "ctype<struct foo>")
