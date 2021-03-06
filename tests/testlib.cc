#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdarg>

#define TEST_STDCALL
#define TEST_FASTCALL

#if defined(_WIN32) || defined(__CYGWIN__)
#  if defined(__i386) || defined(__i386__) || defined(_M_IX86)
#    undef TEST_STDCALL
#    undef TEST_FASTCALL
#    define TEST_STDCALL __stdcall
#    define TEST_FASTCALL __fastcall
#  endif
#  define DLL_EXPORT __declspec(dllexport)
#else
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define DLL_EXPORT __attribute__((visibility("default")))
#  else
#    define DLL_EXPORT
#  endif
#endif

extern "C" DLL_EXPORT
char const test_string[] = "foobar";

extern "C" DLL_EXPORT
int const test_ints[3] = {42, 43, 44};

extern "C" DLL_EXPORT
size_t test_strlen(char const *str) {
    return strlen(str);
}

extern "C" DLL_EXPORT
int test_snprintf(char *buf, size_t n, char const *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = vsnprintf(buf, n, fmt, args);
    va_end(args);
    return ret;
}

extern "C" DLL_EXPORT
int TEST_STDCALL test_stdcall(int a, int b) {
    return a + b;
}

extern "C" DLL_EXPORT
int TEST_FASTCALL test_fastcall(int a, int b) {
    return a + b;
}

struct test_struct {
    int a, b;
};

extern "C" DLL_EXPORT
test_struct test_struct_val(test_struct a, test_struct b) {
    return test_struct{a.a + b.a, a.b + b.b};
}

/* fundamental type passing tests */

/* need to return a struct to read it in lua as bytes */
#define TYPE_TEST(tname, fname) \
    struct test_struct_##fname { tname x; }; \
    extern "C" DLL_EXPORT test_struct_##fname test_##fname(tname v) { \
        test_struct_##fname r; \
        std::memcpy(&r, &v, sizeof(r)); \
        return r; \
    }

TYPE_TEST(unsigned char, uchar)
TYPE_TEST(unsigned short, ushort)
TYPE_TEST(unsigned int, uint)
TYPE_TEST(unsigned long, ulong)
TYPE_TEST(unsigned long long, ullong)
TYPE_TEST(float, float)
TYPE_TEST(double, double)
TYPE_TEST(long double, ldouble)

/* some individual tests without using a record */

extern "C" DLL_EXPORT float test_raw_float(float v) { return v; }
extern "C" DLL_EXPORT char test_raw_char(char c) { return c; }
extern "C" DLL_EXPORT int test_raw_int(int v) { return v; }

/* union value passing tests */

#define UNION_TEST(uname, ubody) \
    union uname ubody; \
    extern "C" DLL_EXPORT uname test_##uname(uname v) { \
        return v; \
    }

UNION_TEST(u1, { signed char a; })
UNION_TEST(u2, { unsigned short a; })
UNION_TEST(u3, { int a; })
UNION_TEST(u4, { long long a; })
UNION_TEST(u5, { float a; })
UNION_TEST(u6, { double a; })
UNION_TEST(u7, { long double a; })

UNION_TEST(us1, { struct { signed char a; } x; })
UNION_TEST(us2, { struct { unsigned short a; } x; })
UNION_TEST(us3, { struct { int a; } x; })
UNION_TEST(us4, { struct { long long a; } x; })
UNION_TEST(us5, { struct { float a; } x; })
UNION_TEST(us6, { struct { double a; } x; })
UNION_TEST(us7, { struct { long double a; } x; })

UNION_TEST(ud1, { signed char a; int b; })
UNION_TEST(ud2, { unsigned short a; long b; })
UNION_TEST(ud3, { int a; signed char b; })
UNION_TEST(ud4, { long long a; float b; })
UNION_TEST(ud5, { float a; long long b; })
UNION_TEST(ud6, { double a; short b; })
UNION_TEST(ud7, { long double a; long b; })

UNION_TEST(ut1, { signed char a; struct { int x; float y; long z; } b; })
UNION_TEST(ut2, { unsigned short a; struct { char x; double y; long double z; } b; })
UNION_TEST(ut3, { int a; struct { int x; int y; int z; } b; })
UNION_TEST(ut4, { long long a; struct { short x; long y; float z; } b; })
UNION_TEST(ut5, { float a; struct { float x; float y; float z; float w; } b; })
UNION_TEST(ut6, { double a; struct { double x; int y; long long z; } b; })
UNION_TEST(ut7, { long double a; struct { long double x; long double y; long double z; } b; })

UNION_TEST(uh1, { struct { double x; double y; } a; struct { double x; double y; double z; } b; })
UNION_TEST(uh2, { double a; struct { double x; double y; } b; })
UNION_TEST(uh3, { struct { double x; struct { double y; double z; } w; } a; struct { double x; double y; double z; } b; })
UNION_TEST(uh4, { struct { double x; struct { double y; double z; } w; } a; struct { double x; } b; })

UNION_TEST(ui1, { struct { int x; int y; } a; struct { int x; int y; int z; } b; })
UNION_TEST(ui2, { int a; struct { int x; int y; } b; })
UNION_TEST(ui3, { struct { int x; struct { int y; int z; } w; } a; struct { int x; int y; int z; } b; })
UNION_TEST(ui4, { struct { int x; struct { int y; int z; } w; } a; struct { int x; } b; })
