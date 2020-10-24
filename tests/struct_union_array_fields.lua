local ffi = require("cffi")

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

    struct qux {
        uint32_t x;
        /* pad 4 */
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

    struct ufoo {
        union {
            uint32_t x;
            uint64_t y[2];
            uint32_t z[2];
        };
    };

    struct ubar {
        union {
            uint8_t x[5];
            int16_t y;
        };
    };
]]

assert(ffi.sizeof("struct foo") == 8)

assert(ffi.sizeof("struct bar") == 3)

assert(ffi.sizeof("struct baz") == 8)

assert(ffi.sizeof("struct baz_flex") == 8)

assert(ffi.sizeof("struct qux") == 32)

assert(ffi.sizeof("struct quux") == 12)

assert(ffi.sizeof("struct quuux") == 20)

assert(ffi.sizeof("struct ufoo") == 16)

assert(ffi.sizeof("struct ubar") == 6)
