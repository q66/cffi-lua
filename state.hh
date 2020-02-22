#ifndef STATE_HH
#define STATE_HH

#include <vector>
#include <unordered_map>
#include <memory>

#include "parser.hh"

namespace state {

/* takes unique ownership of the pointer */
void add_decl(parser::c_object *decl);

parser::c_object const *lookup_decl(std::string const &name);

} /* namespace state */

#endif /* STATE_HH */
