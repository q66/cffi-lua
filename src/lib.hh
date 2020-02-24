#ifndef LIB_HH
#define LIB_HH

#include <string>

namespace lib {

using handle = void *;
using func = void (*)();

handle open(char const *path = nullptr);

void close(handle h);

func get_func(handle h, char const *name);
void *get_var(handle h, char const *name);

} /* namespace lib */

#endif /* LIB_HH */
