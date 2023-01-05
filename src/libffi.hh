/* The point of this header is to abstract away how libffi is included,
 * as there may be multiple ways to include its primary header, as well
 * as include some basic utilities that deal with libffi bits.
 */

#ifndef LIBFFI_HH
#define LIBFFI_HH

#include <cstdarg>
#include <cassert>

#include "platform.hh"
#include "util.hh"

/* Force static linkage against libffi on Windows unless overridden */
#if defined(FFI_WINDOWS_ABI) && !defined(HAVE_LIBFFI_DLLIMPORT)
#  define FFI_BUILDING 1
#endif

#if defined(FFI_DIAGNOSTIC_PRAGMA_CLANG)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

#include <ffi.h>

#if defined(FFI_DIAGNOSTIC_PRAGMA_CLANG)
#pragma clang diagnostic pop
#endif

namespace ffi {

namespace detail {
    struct ffi_stor {
        alignas(alignof(util::max_aligned_t))
        unsigned char data[sizeof(util::biggest_t)];
    };

    template<typename T>
    static inline ffi_type *ffi_int() {
        bool is_signed = util::is_signed<T>::value;
        switch (sizeof(T)) {
            case 8:
                return is_signed ? &ffi_type_sint64 : &ffi_type_uint64;
            case 4:
                return is_signed ? &ffi_type_sint32 : &ffi_type_uint32;
            case 2:
                return is_signed ? &ffi_type_sint16 : &ffi_type_uint16;
            case 1:
                return is_signed ? &ffi_type_sint8 : &ffi_type_uint8;
            default:
                break;
        }
        assert(false);
        return nullptr;
    }
} /* namespace detail */

using scalar_stor_t = detail::ffi_stor;

/* compile-time mappings from builtin types to libffi types
 *
 * this also allows catching bad types at compile time
 */

template<typename T> struct ffi_traits;

template<> struct ffi_traits<void> {
    static ffi_type *type() { return &ffi_type_void; }
};

template<typename T> struct ffi_traits<T *> {
    static ffi_type *type() { return &ffi_type_pointer; }
};

template<typename T> struct ffi_traits<T &> {
    static ffi_type *type() { return &ffi_type_pointer; }
};

template<typename T> struct ffi_traits<T[]> {
    static ffi_type *type() { return &ffi_type_pointer; }
};

template<> struct ffi_traits<bool> {
    static ffi_type *type() { return &ffi_type_uchar; }
};

template<> struct ffi_traits<char> {
    static ffi_type *type() { return detail::ffi_int<char>(); }
};

template<> struct ffi_traits<signed char> {
    static ffi_type *type() { return detail::ffi_int<signed char>(); }
};

template<> struct ffi_traits<unsigned char> {
    static ffi_type *type() { return detail::ffi_int<unsigned char>(); }
};

template<> struct ffi_traits<wchar_t> {
    static ffi_type *type() { return detail::ffi_int<wchar_t>(); }
};

template<> struct ffi_traits<char16_t> {
    static ffi_type *type() { return detail::ffi_int<char16_t>(); }
};

template<> struct ffi_traits<char32_t> {
    static ffi_type *type() { return detail::ffi_int<char32_t>(); }
};

template<> struct ffi_traits<short> {
    static ffi_type *type() { return detail::ffi_int<short>(); }
};

template<> struct ffi_traits<unsigned short> {
    static ffi_type *type() { return detail::ffi_int<unsigned short>(); }
};

template<> struct ffi_traits<int> {
    static ffi_type *type() { return detail::ffi_int<int>(); }
};

template<> struct ffi_traits<unsigned int> {
    static ffi_type *type() { return detail::ffi_int<unsigned int>(); }
};

template<> struct ffi_traits<long> {
    static ffi_type *type() { return detail::ffi_int<long>(); }
};

template<> struct ffi_traits<unsigned long> {
    static ffi_type *type() { return detail::ffi_int<unsigned long>(); }
};

template<> struct ffi_traits<long long> {
    static ffi_type *type() { return detail::ffi_int<long long>(); }
};

template<> struct ffi_traits<unsigned long long> {
    static ffi_type *type() { return detail::ffi_int<unsigned long long>(); }
};

template<> struct ffi_traits<float> {
    static ffi_type *type() { return &ffi_type_float; }
};

template<> struct ffi_traits<double> {
    static ffi_type *type() { return &ffi_type_double; }
};

template<> struct ffi_traits<long double> {
    static ffi_type *type() { return &ffi_type_longdouble; }
};

template<typename T>
struct ffi_traits<T const>: ffi_traits<T> {};

template<typename T>
struct ffi_traits<T volatile>: ffi_traits<T> {};

template<typename T>
struct ffi_traits<T const volatile>: ffi_traits<T> {};

} /* namespace ffi */

#endif /* LIBFFI_HH */
