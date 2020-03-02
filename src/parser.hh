#ifndef PARSER_HH
#define PARSER_HH

#include <cstdint>

#include <string>
#include <vector>
#include <memory>

#include "lua.hh"
#include "ast.hh"

namespace parser {

union lex_token_u {
    char c;
    signed int i;
    signed long l;
    signed long long ll;
    unsigned int u;
    unsigned long ul;
    unsigned long long ull;
    float f;
    double d;
};

void parse(lua_State *L, char const *input, char const *iend = nullptr);

static inline void parse(lua_State *L, std::string const &input) {
    parse(L, input.c_str(), input.c_str() + input.size());
}

ast::c_type parse_type(
    lua_State *L, char const *input, char const *iend = nullptr
);

static inline ast::c_type parse_type(lua_State *L, std::string const &input) {
    return parse_type(L, input.c_str(), input.c_str() + input.size());
}

ast::c_expr_type parse_number(
    lua_State *L, lex_token_u &v, char const *input, char const *iend = nullptr
);

static inline ast::c_expr_type parse_number(
    lua_State *L, lex_token_u &v, std::string const &input
) {
    return parse_number(L, v, input.c_str(), input.c_str() + input.size());
}

}; /* namespace parser */

#endif /* PARSER_HH */
