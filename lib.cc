/* TODO: implement for windows */

#include <dlfcn.h>

#include "lib.hh"

namespace lib {

handle open(char const *path) {
    return dlopen(path, RTLD_NOW);
}

void close(handle h) {
    dlclose(h);
}

func get_func(handle h, char const *name) {
    return reinterpret_cast<func>(dlsym(h, name));
}

void *get_var(handle h, char const *name) {
    return dlsym(h, name);
}

} /* namespace lib */
