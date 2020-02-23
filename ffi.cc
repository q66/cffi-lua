#include <limits>

#include "ffi.hh"

namespace ffi {

template<typename T>
static inline bool use_ffi_signed(int cv) {
    if (cv & parser::C_CV_SIGNED) {
        return true;
    }
    if (cv & parser::C_CV_UNSIGNED) {
        return false;
    }
    return std::numeric_limits<T>::is_signed;
}

ffi_type *get_ffi_type(parser::c_type const &tp) {
    int cv = tp.cv();

#define INT_CASE(bname, rtype, ftype) \
    case parser::C_BUILTIN_##bname: \
        if (use_ffi_signed<rtype>(cv)) { \
            return &ffi_type_s##ftype; \
        } else { \
            return &ffi_type_u##ftype; \
        }

#define INT_CASE64(bname, rtype) \
    case parser::C_BUILTIN_##bname: \
        if (sizeof(rtype) == 8) { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint64; \
            } else { \
                return &ffi_type_uint64; \
            } \
        } else if (sizeof(rtype) == 4) { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint32; \
            } else { \
                return &ffi_type_uint32; \
            } \
        } else if (sizeof(rtype) == 2) { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint16; \
            } else { \
                return &ffi_type_uint16; \
            } \
        } else { \
            if (cv & parser::C_CV_SIGNED) { \
                return &ffi_type_sint8; \
            } else { \
                return &ffi_type_uint8; \
            } \
        }

    switch (tp.type()) {
        case parser::C_BUILTIN_PTR:
            printf("pointer\n");
            return &ffi_type_pointer;

        INT_CASE(CHAR, char, char)
        INT_CASE(SHORT, short, short)
        INT_CASE(INT, int, int)
        INT_CASE(LONG, long, long)
        INT_CASE(LLONG, int64_t, int64)
    
        INT_CASE(INT8, int8_t, int8)
        INT_CASE(INT16, int16_t, int16)
        INT_CASE(INT32, int32_t, int32)
        INT_CASE(INT64, int64_t, int64)

        INT_CASE64(SIZE, size_t)
        INT_CASE64(INTPTR, intptr_t)

        case parser::C_BUILTIN_TIME:
            /* FIXME: time_t may be represented in other ways too */
            if (sizeof(time_t) == 8) {
                return &ffi_type_sint64;
            } else {
                return &ffi_type_sint32;
            }

        case parser::C_BUILTIN_FLOAT:
            return &ffi_type_float;
        case parser::C_BUILTIN_DOUBLE:
            return &ffi_type_double;
        case parser::C_BUILTIN_LDOUBLE:
            /* FIXME: this may not be defined */
            return &ffi_type_longdouble;

        case parser::C_BUILTIN_BOOL:
            /* hmm... */
            return &ffi_type_uchar;

        default:
            break;
    }

#undef INT_CASE

    /* TODO: custom types */
    return &ffi_type_sint;
}

} /* namespace ffi */
