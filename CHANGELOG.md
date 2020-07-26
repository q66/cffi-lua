# 26 July 2020 - 0.1.0 - Initial release

This is the initial release of the `cffi` module. It is of beta quality.
See `STATUS.md` for status and `README.md` for building and usage.

Production usage is currently not recommended, since the module is not
battle tested. Testing is highly encouraged.

Current requirements:

- Lua 5.1 - 5.4
- `libffi`
- `meson` 0.50+, optional `pkg-config`
- GCC 7+, Clang 8+ or Visual Studio 2017+ with updates recommended
  - Theoretical minimum is GCC 4.8, Clang 3.4 or any compiler with enough C++14
