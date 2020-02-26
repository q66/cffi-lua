#ifndef PARSER_HH
#define PARSER_HH

#include <cstdint>

#include <string>
#include <vector>
#include <memory>

#include "ast.hh"

namespace parser {

void parse(char const *input, char const *iend = nullptr);
void parse(std::string const &input);

ast::c_type parse_type(char const *input, char const *iend = nullptr);
ast::c_type parse_type(std::string const &input);

}; /* namespace parser */

#endif /* PARSER_HH */
