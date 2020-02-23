#ifndef FFI_HH
#define FFI_HH

#include <string>

#include <ffi.h>

#include "parser.hh"

namespace ffi {

ffi_type *get_ffi_type(parser::c_type const &tp);

} /* namespace ffi */

#endif /* FFI_HH */
