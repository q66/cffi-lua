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

union test_union {
    int a;
    double b;
};

union test_union2 {
    double a;
    double b;
};

extern "C" DLL_EXPORT
test_struct test_struct_val(test_struct a, test_struct b) {
    return test_struct{a.a + b.a, a.b + b.b};
}

extern "C" DLL_EXPORT
test_union test_union_val(test_union a, test_union b) {
    test_union ret;
    ret.a = int(a.b + b.b);
    return ret;
}

extern "C" DLL_EXPORT
test_union2 test_union_val2(test_union2 a) {
    test_union2 ret;
    ret.b = a.a;
    return ret;
}
