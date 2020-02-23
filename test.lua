local ffi = require("cffi")

ffi.cdef [[
    //long int strtol(const char *str, char **endptr, int base);
    int puts(char const *str);
]]

print(ffi.C)

ret = ffi.C.puts("hello world")
print("end test, ret:", ret);
