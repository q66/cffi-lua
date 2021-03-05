local ffi = require("cffi")

local field_size = function(tp, t2)
    local sz = ffi.sizeof(tp)
    if not t2 then
        return sz
    end
    local al = ffi.alignof(t2)
    return math.floor((sz + al - 1) / al) * al
end

local expected_sz
local exepcted_al

ffi.cdef [[
    struct foo {
        uint8_t x;
        uint32_t y;
    };

    struct bar {
        uint8_t x[3];
    };

    struct baz {
        uint32_t x;
        uint8_t y[4];
    };

    struct baz_flex {
        uint32_t x;
        uint8_t y[2];
        char z[];
    };

    /* flex members may add padding but do not add their own size */
    struct pad_flex {
        uint8_t x;
        uint64_t y[];
    };

    struct qux {
        uint32_t x;
        /* on most platforms, pad 4 */
        uint64_t y[2];
        uint32_t z[2];
    };

    struct quux {
        uint32_t x;
        struct iquux {
            uint8_t y[4];
        } y[2];
    };

    struct quuux {
        uint32_t x;
        struct bar y;
        struct quux z;
    };

    union ufoo_u {
        uint32_t x;
        uint64_t y[2];
        uint32_t z[2];
    };

    struct ufoo {
        union {
            uint32_t x;
            uint64_t y[2];
            uint32_t z[2];
        };
    };

    union ubar_u {
        uint8_t x[5];
        int16_t y;
    };

    struct ubar {
        union {
            uint8_t x[5];
            int16_t y;
        };
    };
]]

-- struct foo

expected_sz = (
    field_size("uint8_t", "uint32_t") +
    field_size("uint32_t")
)
assert(ffi.sizeof("struct foo") == expected_sz)

-- struct bar

expected_sz = field_size("uint8_t") * 3
assert(ffi.sizeof("struct bar") == expected_sz)

-- struct baz

expected_sz = (
    field_size("uint32_t", "uint8_t") +
    field_size("uint8_t") * 4
)
assert(ffi.sizeof("struct baz") == expected_sz)

-- struct baz_flex

expected_sz = (
    field_size("uint32_t", "uint8_t") +
    field_size("uint8_t") * 2
)
-- pad to multiple of alignment of uint32_t
local uint32_al = ffi.alignof("uint32_t")
expected_sz = math.floor((expected_sz + uint32_al - 1) / uint32_al) * uint32_al
assert(ffi.sizeof("struct baz_flex") == expected_sz)

-- struct pad_flex

expected_sz = (
    field_size("uint8_t", "uint64_t")
)
assert(ffi.sizeof("struct pad_flex") == expected_sz)

-- struct qux

expected_sz = (
    field_size("uint32_t", "uint64_t") +
    field_size("uint64_t") * 2 +
    field_size("uint32_t[2]", "uint64_t")
)
assert(ffi.sizeof("struct qux") == expected_sz)

-- struct quux

expected_sz = (
    field_size("uint32_t", "struct { uint8_t x[4]; }") +
    field_size("struct { uint8_t x[4]; }") * 2
)
assert(ffi.sizeof("struct quux") == expected_sz)

-- struct quuux

expected_sz = (
    field_size("uint32_t", "struct bar") +
    field_size("struct bar", "struct quux") +
    field_size("struct quux")
)
assert(ffi.sizeof("struct quuux") == expected_sz)

-- union ufoo_u, struct ufoo

expected_sz = math.max(
    field_size("uint32_t"),
    field_size("uint64_t[2]"),
    field_size("uint32_t[2]")
)
expected_al = math.max(
    ffi.alignof("uint32_t"),
    ffi.alignof("uint64_t")
)
assert(ffi.sizeof("union ufoo_u") == expected_sz)
assert(ffi.alignof("union ufoo_u") == expected_al)
assert(ffi.sizeof("struct ufoo") == expected_sz)
assert(ffi.alignof("struct ufoo") == expected_al)
assert((expected_sz % expected_al) == 0)

-- union ubar_u, struct ubar

local int16_al = ffi.alignof("int16_t")

expected_sz = math.max(
    math.floor((field_size("uint8_t[5]") + int16_al - 1) / int16_al) * int16_al,
    field_size("int16_t")
)
expected_al = math.max(
    ffi.alignof("uint8_t"),
    int16_al
)
print(ffi.sizeof("union ubar_u"), expected_sz)
assert(ffi.sizeof("union ubar_u") == expected_sz)
assert(ffi.alignof("union ubar_u") == expected_al)
assert(ffi.sizeof("struct ubar") == expected_sz)
assert(ffi.alignof("struct ubar") == expected_al)
assert((expected_sz % expected_al) == 0)

