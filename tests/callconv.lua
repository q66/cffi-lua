-- calling convention specifiers are ignored except on 32-bit x86 windows

local ffi = require("cffi")

local L = require("testlib")

ffi.cdef [[
    int __stdcall test_stdcall(int, int);
    int test_fastcall1(int, int) __attribute__((fastcall)) __asm__("test_fastcall");
    __attribute__((fastcall)) int test_fastcall2(int, int) __asm__("test_fastcall");
    int __fastcall test_fastcall3(int, int) __asm__("test_fastcall");
    int __attribute__((fastcall)) test_fastcall4(int, int) __asm__("test_fastcall");
]]

local r = L.test_stdcall(5, 10)
assert(r == 15)

local r = L.test_fastcall1(5, 10)
assert(r == 15)

local r = L.test_fastcall2(5, 10)
assert(r == 15)

local r = L.test_fastcall3(5, 10)
assert(r == 15)

local r = L.test_fastcall4(5, 10)
assert(r == 15)

local fp1 = ffi.cast("int (__fastcall *)(int, int)", L.test_fastcall1)
local fp2 = ffi.cast("int (*)(int, int) __attribute__((fastcall))", L.test_fastcall1)

assert(fp1(15, 20) == 35)
assert(fp2(15, 20) == 35)
