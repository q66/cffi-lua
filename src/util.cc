#include <cstdlib>

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
