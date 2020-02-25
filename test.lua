local ffi = require("cffi")

ffi.cdef [[
    double strtod(const char *str, char **endptr);
    int puts(char const *str);
    char *strdup(char const *x);
    void *memcpy(void *dest, const void *src, size_t num);
    void free(void *p);
]]

print("## BEGIN TESTS ##")
print()

print("# main library namespace")
print("namespace data: " .. tostring(ffi.C))
print()

print("# string printing")
ret = ffi.C.puts("hello world")
print("received from puts: " .. ret)
print()

print("# strtod")
test = ffi.C.strtod(tostring(22 / 7), nil)
print("strtod(tostring(22 / 7)) == " .. tostring(test))
print()

print("# invalid conversion")
print(pcall(ffi.C.strtod, 150, nil))
print()

print("# strdup")
input = "some random string"
print("input: " .. input)

foo = ffi.C.strdup(input)
print("dup'd: " .. tostring(foo))

ffi.C.memcpy(foo, "<redacted>", 10);
bar = ffi.string(foo)
print("converted back to string: " .. bar)
ffi.C.free(foo)
print()

print("## END TESTS ##")
