#include <cstdlib>
#include <climits>

#include "util.hh"

void *operator new(size_t n) {
    void *p = malloc(n);
    if (!p) {
        abort(); /* FIXME: do not abort */
    }
    return p;
}

void *operator new[](size_t n) {
    void *p = malloc(n);
    if (!p) {
        abort();
    }
    return p;
}

void operator delete(void *p) {
    free(p);
}

void operator delete[](void *p) {
    free(p);
}

void operator delete(void *p, size_t) {
    free(p);
}

void operator delete[](void *p, size_t) {
    free(p);
}

namespace util {

size_t write_i(char *buf, size_t bufsize, long long v) {
    if (v < 0) {
        if (!bufsize) {
            return write_u(
                buf, bufsize, static_cast<unsigned long long>(-v)
            ) + 1;
        }
        *buf = '-';
        return write_u(
            buf + 1, bufsize - 1, static_cast<unsigned long long>(-v)
        ) + 1;
    }
    return write_u(buf, bufsize, static_cast<unsigned long long>(v));
}

size_t write_u(char *bufp, size_t bufsize, unsigned long long v) {
    char buf[sizeof(unsigned long long) * CHAR_BIT];
    size_t ndig = 0;
    if (!v) {
        buf[0] = '0';
        ndig = 1;
    } else {
        for (; v; v /= 10) {
            buf[ndig++] = char(v % 10) + '0';
        }
    }
    if (bufsize < (ndig + 1)) {
        return ndig;
    }
    for (size_t i = 0; i < ndig; ++i) {
        *bufp++ = buf[ndig - i - 1];
    }
    *bufp++ = '\0';
    return ndig;
}

} /* namespace util */
