local ffi = require("cffi")

ffi.cdef [[
    long int strtol(const char *str, char **endptr, int base);
]]

print(ffi.C)
