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

local p = ffi.gc(ffi.C.malloc(4), ffi.C.free)
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
    struct foo typedef foo;
    typedef struct bar foo;
]]

assert(tostring(ffi.typeof("foo")) == "ctype<struct foo>")

-- https://github.com/q66/cffi-lua/issues/11
-- make sure non-conflicting names are picked for structs even
-- when nested (as outer struct is still being parsed and thus
-- is not present in the declaration store yet)

ffi.cdef [[
    typedef struct {
        struct {
            uint32_t a;
        } inner1;
    } foo_t;

    typedef struct {
        struct {
            uint32_t b;
        } inner2;
    } bar_t;
]]

-- https://github.com/q66/cffi-lua/issues/12
-- make sure requested names are not repeated across commits

ffi.cdef [[
    enum {FOO = 1};
    enum {BAR = 2};
]]

ffi.cdef [[
    enum {QUX = 3};
]]
