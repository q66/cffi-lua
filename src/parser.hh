#ifndef PARSER_HH
#define PARSER_HH

#include <cstdint>

#include <string>
#include <memory>

#include "lua.hh"
#include "ast.hh"

namespace parser {

void parse(
    lua_State *L, char const *input, char const *iend = nullptr, int paridx = -1
);

static inline void parse(
    lua_State *L, std::string const &input, int paridx = -1
) {
    parse(L, input.c_str(), input.c_str() + input.size(), paridx);
}

ast::c_type parse_type(
    lua_State *L, char const *input, char const *iend = nullptr, int paridx = -1
);

static inline ast::c_type parse_type(
    lua_State *L, std::string const &input, int paridx = -1
) {
    return parse_type(
        L, input.c_str(), input.c_str() + input.size(), paridx
    );
}

ast::c_expr_type parse_number(
    lua_State *L, ast::c_value &v, char const *input, char const *iend = nullptr
);

static inline ast::c_expr_type parse_number(
    lua_State *L, ast::c_value &v, std::string const &input
) {
    return parse_number(L, v, input.c_str(), input.c_str() + input.size());
}

} /* namespace parser */

#endif /* PARSER_HH */
