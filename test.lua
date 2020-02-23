local ffi = require("cffi")

ffi.cdef [[
    double strtod(const char *str, char **endptr);
    int puts(char const *str);
]]

print(ffi.C)

ret = ffi.C.puts("hello world")
print("end test, ret:" .. tostring(ret))

test = ffi.C.strtod(tostring(22 / 7), nil)
print("strtod (tostring(22 / 7)) == " .. tostring(test))

print(pcall(ffi.C.strtod, 150, nil))
