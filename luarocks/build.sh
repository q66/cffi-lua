#!/bin/sh

set -e

# luarocks more like luasucks
case "$CC" in
    *-cc)    export CXX=${CC/-cc/-c++}      ;;
    cc)      export CXX=c++                 ;;
    *-gcc)   export CXX=${CC/-gcc/-g++}     ;;
    gcc)     export CXX=g++                 ;;
    *clang*) export CXX=${CC/clang/clang++} ;;
    *)       export CXX=$CC                 ;;
esac

# should be okay
export CXXFLAGS="$CFLAGS"

case "$1" in
    build)
        rm -rf build && mkdir build && cd build
        meson .. -Dbuildtype=release -Dlua_version=custom \
            -Dtests=false -Dlua_install_path="$LIBDIR" \
            --force-fallback-for=libffi
        ninja all
        ;;
    install)
        # we want just the module, no subproject stuff
        mkdir -p "$LIBDIR"
        cp build/cffi.so "$LIBDIR"
        mkdir -p "${PREFIX}"/doc
        cp -a docs/* "${PREFIX}"/doc
        rm -rf build
        ;;
    *) exit 1 ;;
esac

exit 0
