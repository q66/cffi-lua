# 1 November 2022 - 0.2.2

**Changes:**

- Passing unions by value is now permitted on specific platforms:
  - Windows (x86 and x86_64), MIPS32/64, all 32-bit ARM, PowerPC
- Initializer fixes in `ffi.new`
- Fixed unary metamethods on newer versions of Lua
- Expanded architecture identifier coverage
- CI updates
- Various fixes and cleanups

# 31 January 2021 - 0.2.1

**Changes:**

- Fixed multidimensional arrays in structs
- Fixed release builds with `NDEBUG`
- CI updates

# 29 November 2020 - 0.2.0

**Changes:**

- No longer using the C++ standard library, exceptions, RTTI and TLS
- Lower resource utilization, simpler memory management
- Constant expressions now properly handle errors
- A large amount of fixes (unhandled cases, LuaJIT FFI compatibility, etc.)
- Greatly expanded test suite
- Removed custom test runner (using just Lua)

**Requirements:**

- No changes

# 01 August 2020 - 0.1.1

**Changes:**

- Build system enhancements
- CI enhancements
- We no longer ship a library/API (except a static library for embedded uses)
- Initial LuaRocks support (Windows support pending)
- Added option to disable tests

**Requirements:**

- No changes

**Library clarification:**

By default, you will only get a Lua module now. You can pass `-Dstatic=true`
to `meson` to switch the build to a static library; you will then get that
instead of a module. This is intended for systems that don't have Lua module
support - there you will want to link the FFI into the application instead.

It is not possible to build the static library and the module at the same
time, and usually the recommended thing is to build the module.

# 26 July 2020 - 0.1.0 - Initial release

This is the initial release of the `cffi` module. It is of beta quality.
See `STATUS.md` for status and `README.md` for building and usage.

Production usage is currently not recommended, since the module is not
battle tested. Testing is highly encouraged.

**Requirements:**

- Lua 5.1 - 5.4
- `libffi`
- `meson` 0.50+, optional `pkg-config`
- GCC 7+, Clang 8+ or Visual Studio 2017+ with updates recommended
  - Theoretical minimum is GCC 4.8, Clang 3.4 or any compiler with enough C++14
