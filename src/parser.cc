#include <cstdint>
#include <cstring>
#include <cassert>
#include <ctime>

#include "parser.hh"
#include "ast.hh"
#include "util.hh"

namespace parser {

/* define all keywords our subset of C understands */

/* stdint types might as well also be builtin... */
#define KEYWORDS KW(alignof), KW(alignas), KW(auto), KW(const), KW(enum), \
    KW(extern), KW(sizeof), KW(struct), KW(signed), KW(typedef), KW(union), \
    KW(unsigned), KW(volatile), KW(void), \
    \
    KW(_Alignas), \
    \
    KW(__alignof__), KW(__const__), KW(__volatile__), \
    \
    KW(__attribute__), KW(__extension__), KW(__asm__), \
    \
    KW(__declspec), KW(__cdecl), KW(__fastcall), KW(__stdcall), \
    KW(__thiscall), KW(__ptr32), KW(__ptr64), \
    \
    KW(true), KW(false), \
    \
    KW(bool), KW(char), KW(char16_t), KW(char32_t), KW(short), KW(int), \
    KW(long), KW(wchar_t), KW(float), KW(double), \
    \
    KW(int8_t), KW(uint8_t), KW(int16_t), KW(uint16_t), \
    KW(int32_t), KW(uint32_t), KW(int64_t), KW(uint64_t), \
    \
    KW(size_t), KW(ssize_t), KW(intptr_t), KW(uintptr_t), \
    KW(ptrdiff_t), KW(time_t), \
    \
    KW(va_list), KW(__builtin_va_list), KW(__gnuc_va_list), \
    \
    KW(_Bool)

/* primary keyword enum */

#define KW(x) TOK_##x

/* a token is an int, single-char tokens are just their ascii */
/* TOK_NAME must be the first pre-keyword token! */
enum c_token {
    TOK_CUSTOM = 257,

    TOK_EQ = TOK_CUSTOM, TOK_NEQ, TOK_GE, TOK_LE,
    TOK_AND, TOK_OR, TOK_LSH, TOK_RSH,

    TOK_ELLIPSIS, TOK_ATTRIBB, TOK_ATTRIBE, TOK_ARROW,

    TOK_INTEGER, TOK_FLOAT, TOK_CHAR, TOK_STRING, TOK_NAME, KEYWORDS
};

#undef KW

/* end primary keyword enum */

/* token strings */

#define KW(x) #x

static char const *tokens[] = {
    "==", "!=", ">=", "<=",
    "&&", "||", "<<", ">>",

    "...", "((", "))", "->",

    "<integer>", "<float>", "<char>", "<string>", "<name>", KEYWORDS
};

#undef KW

/* end token strings */

/* lexer */

struct lex_token {
    int token = -1;
    ast::c_expr_type numtag = ast::c_expr_type::INVALID;
    ast::c_value value{};
};

/* represents a level of parens pair when parsing a type; so e.g. in
 *
 * void (*(*(*(*&))))
 *
 * we have 4 levels.
 */
struct parser_type_level {
    parser_type_level():
        arrd{0}, cv{0}, flags{0}, is_term{false}, is_func{false},
        is_ref{false}
    {}
    ~parser_type_level() {
        if (is_func) {
            using DT = util::vector<ast::c_param>;
            argl.~DT();
        }
    }
    parser_type_level(parser_type_level &&v):
        cv{v.cv}, flags{v.flags}, cconv{v.cconv}, is_term{v.is_term},
        is_func{v.is_func}, is_ref{v.is_ref}
    {
        if (is_func) {
            new (&argl) util::vector<ast::c_param>(util::move(v.argl));
        } else {
            arrd = v.arrd;
        }
    }

    union {
        util::vector<ast::c_param> argl;
        std::size_t arrd;
    };
    std::uint32_t cv: 2;
    std::uint32_t flags: 6;
    std::uint32_t cconv: 6;
    std::uint32_t is_term: 1;
    std::uint32_t is_func: 1;
    std::uint32_t is_ref: 1;
};

/* this stack stores whatever parse_array parses, the number of elements
 * per single parse_type_ptr call is stored above in the level struct; each
 * level with non-zero arrd will pop off arrd items
 */
struct parser_array_dim {
    std::size_t size;
    std::uint32_t quals;
};

/* global parser state, one per lua_State * */
struct parser_state {
    /* a mapping from keyword id to keyword name, populated on init */
    util::str_map<int> keyword_map;
    /* all-purpose string buffer used when parsing, also for error messages */
    util::strbuf ls_buf;
    /* used when parsing types */
    util::vector<parser_type_level> plevel_queue{};
    util::vector<parser_array_dim> arrdim_stack{};
    /* stores the token id when throwing errors */
    int err_token;
    /* stores the line number when throwing errors */
    int err_lnum;
};

static void init_kwmap(util::str_map<int> &km) {
    if (!km.empty()) {
        return;
    }
    auto nkw = int(
        sizeof(tokens) / sizeof(tokens[0]) + TOK_CUSTOM - TOK_NAME - 1
    );
    for (int i = 1; i <= nkw; ++i) {
        km[tokens[TOK_NAME - TOK_CUSTOM + i]] = i;
    }
}

enum parse_mode {
    PARSE_MODE_DEFAULT,
    PARSE_MODE_TYPEDEF,
    PARSE_MODE_NOTCDEF,
    PARSE_MODE_ATTRIB,
};

/* locale independent ctype replacements */

inline int is_digit(int c) {
    return (c >= '0') && (c <= '9');
}

inline int is_hex_digit(int c) {
    c |= 32; /* make lowercase */
    return is_digit(c) || ((c >= 'a') && (c <= 'f'));
}

inline int is_space(int c) {
    return (
        (c ==  ' ') || (c == '\t') || (c == '\n') ||
        (c == '\v') || (c == '\f') || (c == '\r')
    );
}

inline int is_alpha(int c) {
    c |= 32; /* lowercase */
    return ((c >= 'a') && (c <= 'z'));
}

inline int is_alphanum(int c) {
    return is_alpha(c) || is_digit(c);
}

inline int is_print(int c) {
    /* between Space and ~ */
    return (c >= 0x20) && (c <= 0x7E);
}

struct lex_state {
    lex_state() = delete;

    lex_state(
        lua_State *L, char const *str, const char *estr,
        int pmode = PARSE_MODE_DEFAULT, int paridx = -1
    ):
        p_mode(pmode), p_pidx(paridx), p_L(L), stream(str),
        send(estr), p_dstore{ast::decl_store::get_main(L)}
    {
        lua_getfield(L, LUA_REGISTRYINDEX, lua::CFFI_PARSER_STATE);
        if (!lua_isuserdata(L, -1)) {
            luaL_error(L, "internal error: no parser state");
        }
        p_P = lua::touserdata<parser_state>(L, -1);
        if (!p_P) {
            luaL_error(L, "internal error: parser state is null");
        }
        lua_pop(L, 1);

        /* this should be enough that we should never have to resize it */
        p_P->ls_buf.clear();
        p_P->ls_buf.reserve(256);

        /* read first char */
        next_char();

        /* skip past potential UTF-8 BOM */
        if (current != 0xEF) {
            return;
        }
        next_char();
        if (current != 0xBB) {
            return;
        }
        next_char();
        if (current != 0xBF) {
            return;
        }
        next_char();
    }

    ~lex_state() {}

    bool get() WARN_UNUSED_RET {
        if (lahead.token >= 0) {
            t = util::move(lahead);
            lahead.token = -1;
            return true;
        }
        t.token = lex(t);
        return !!t.token;
    }

    bool lookahead(int &tok) WARN_UNUSED_RET {
        tok = lahead.token = lex(t);
        return !!tok;
    }

    bool lex_error(int tok, int linenum) WARN_UNUSED_RET {
        p_P->err_token = tok;
        p_P->err_lnum = linenum;
        return false;
    }

    bool lex_error(int tok) WARN_UNUSED_RET {
        return lex_error(tok, line_number);
    }

    bool syntax_error() WARN_UNUSED_RET {
        return lex_error(t.token);
    }

    bool store_decl(ast::c_object *obj, int lnum) WARN_UNUSED_RET {
        auto *old = p_dstore.add(obj);
        if (old) {
            p_P->ls_buf.clear();
            p_P->ls_buf.append('\'');
            p_P->ls_buf.append(old->name());
            p_P->ls_buf.append("' redefined");
            return lex_error(-1, lnum);
        }
        return true;
    }

    void commit() {
        p_dstore.commit();
    }

    ast::c_object const *lookup(char const *name) const {
        return p_dstore.lookup(name);
    }

    ast::c_object *lookup(char const *name) {
        return p_dstore.lookup(name);
    }

    std::size_t request_name(char *buf, std::size_t bufsize) {
        return p_dstore.request_name(buf, bufsize);
    }

    int mode() const {
        return p_mode;
    }

    int mode(int nmode) {
        int ret = p_mode;
        p_mode = nmode;
        return ret;
    }

    bool param_maybe_name() WARN_UNUSED_RET {
        if (t.token != '$') {
            return true;
        }
        if (!ensure_pidx()) {
            return false;
        }
        std::size_t len;
        char const *str = lua_tolstring(p_L, p_pidx, &len);
        if (!str) {
            p_P->ls_buf.set("name expected");
            return syntax_error();
        }
        /* replace $ with name */
        t.token = TOK_NAME;
        p_P->ls_buf.set(str, len);
        ++p_pidx;
        return true;
    }

    /* FIXME: very preliminary, should support more stuff, more types */
    bool param_maybe_expr() WARN_UNUSED_RET {
        if (t.token != '$') {
            return true;
        }
        if (!ensure_pidx()) {
            return false;
        }
        lua_Integer d = lua_tointeger(p_L, p_pidx);
        if (!d && !lua_isnumber(p_L, p_pidx)) {
            p_P->ls_buf.set("integer expected");
            return syntax_error();
        }
        /* replace $ with integer */
        t.token = TOK_INTEGER;
        if (d < 0) {
            t.numtag = ast::c_expr_type::LLONG;
            t.value.ll = d;
        } else {
            t.numtag = ast::c_expr_type::ULLONG;
            t.value.ull = d;
        }
        ++p_pidx;
        return true;
    }

    bool param_get_type(ast::c_type &res) WARN_UNUSED_RET {
        if (!ensure_pidx()) {
            return false;
        }
        if (!luaL_testudata(p_L, p_pidx, lua::CFFI_CDATA_MT)) {
            p_P->ls_buf.set("type expected");
            return syntax_error();
        }
        res = lua::touserdata<ast::c_type>(p_L, p_pidx)->copy();
        /* consume $ */
        if (!get()) {
            return false;
        }
        ++p_pidx;
        return true;
    }

    lua_State *lua_state() const {
        return p_L;
    }

    util::strbuf &get_buf() {
        return p_P->ls_buf;
    }

    util::strbuf const &get_buf() const {
        return p_P->ls_buf;
    }

    int err_token() const {
        return p_P->err_token;
    }

    int err_line() const {
        return p_P->err_lnum;
    }

    util::vector<parser_type_level> &type_level_queue() {
        return p_P->plevel_queue;
    }

    util::vector<parser_array_dim> &array_dim_stack() {
        return p_P->arrdim_stack;
    }

private:
    bool ensure_pidx() WARN_UNUSED_RET {
        if ((p_pidx <= 0) || lua_isnone(p_L, p_pidx)) {
            p_P->ls_buf.set("wrong number of type parameters");
            return syntax_error();
        }
        return true;
    }

    bool is_newline(int c) {
        return (c == '\n') || (c == '\r');
    }

    char next_char() {
        char ret = char(current);
        if (stream == send) {
            current = '\0';
            return ret;
        }
        current = *(stream++);
        return ret;
    }

    char upcoming() const {
        if (stream == send) {
            return '\0';
        }
        return *stream;
    }

    void next_line() {
        int old = current;
        next_char();
        if (is_newline(current) && current != old) {
            next_char();
        }
        ++line_number; /* FIXME: handle overflow */
    }

    template<typename T>
    bool check_int_fits(unsigned long long val) {
        using U = unsigned long long;
        return (val <= U(~T(0)));
    }

    /* this doesn't deal with stuff like negative values at all, that's
     * done in the expression parser as a regular unary expression and
     * is subject to the standard rules
     */
    ast::c_expr_type get_int_type(
        lex_token &tok, unsigned long long val, bool decimal
    ) {
        bool unsig = false;
        int use_long = 0;
        if ((current | 32) == 'u') {
            unsig = true;
            next_char();
            if ((current | 32) == 'l') {
                ++use_long;
                next_char();
                if ((current | 32) == 'l') {
                    ++use_long;
                    next_char();
                }
            }
        } else if ((current | 32) == 'l') {
            ++use_long;
            next_char();
            if ((current | 32) == 'l') {
                ++use_long;
                next_char();
            }
            if ((current | 32) == 'u') {
                unsig = true;
            }
        }
        /* decimals still allow explicit unsigned, for others it's implicit */
        bool aus = (unsig || !decimal);
        switch (use_long) {
            case 0:
                /* no long suffix, can be any size */
                if (!unsig && check_int_fits<int>(val)) {
                    tok.value.i = static_cast<int>(val);
                    return ast::c_expr_type::INT;
                } else if (aus && check_int_fits<unsigned int>(val)) {
                    tok.value.u = static_cast<unsigned int>(val);
                    return ast::c_expr_type::UINT;
                } else if (!unsig && check_int_fits<long>(val)) {
                    tok.value.l = static_cast<long>(val);
                    return ast::c_expr_type::LONG;
                } else if (aus && check_int_fits<unsigned long>(val)) {
                    tok.value.ul = static_cast<unsigned long>(val);
                    return ast::c_expr_type::ULONG;
                } else if (!unsig && check_int_fits<long long>(val)) {
                    tok.value.ll = static_cast<long long>(val);
                    return ast::c_expr_type::LLONG;
                } else if (aus) {
                    tok.value.ull = static_cast<unsigned long long>(val);
                    return ast::c_expr_type::ULLONG;
                }
                break;
            case 1:
                /* l suffix */
                if (!unsig && check_int_fits<long>(val)) {
                    tok.value.l = static_cast<long>(val);
                    return ast::c_expr_type::LONG;
                } else if (aus && check_int_fits<unsigned long>(val)) {
                    tok.value.ul = static_cast<unsigned long>(val);
                    return ast::c_expr_type::ULONG;
                }
                break;
            case 2:
                /* ll suffix */
                if (!unsig && check_int_fits<long long>(val)) {
                    tok.value.ll = static_cast<long long>(val);
                    return ast::c_expr_type::LLONG;
                } else if (aus) {
                    tok.value.ull = static_cast<unsigned long long>(val);
                    return ast::c_expr_type::ULLONG;
                }
                break;
            default:
                break;
        }
        /* unsuffixed decimal and doesn't fit into signed long long,
         * or explicitly marked long and out of bounds
         */
        p_P->ls_buf.set("value out of bounds");
        if (!lex_error(TOK_INTEGER)) {
            return ast::c_expr_type::INVALID;
        }
        /* unreachable */
        return ast::c_expr_type::INVALID;
    }

    template<std::size_t base, typename F, typename G>
    bool read_int_core(F &&digf, G &&convf, lex_token &tok) {
        auto &lb = p_P->ls_buf.raw();
        lb.clear();
        do {
            lb.push_back(next_char());
        } while (digf(current));
        char const *numbeg = &p_P->ls_buf[0], *numend = &p_P->ls_buf[lb.size()];
        /* go from the end */
        unsigned long long val = 0, mul = 1;
        do {
            /* standardize case */
            int dig = convf(*--numend);
            val += dig * mul;
            mul *= base;
        } while (numend != numbeg);
        /* write type and value */
        tok.numtag = get_int_type(tok, val, base == 10);
        return (tok.numtag != ast::c_expr_type::INVALID);
    }

    bool read_integer(lex_token &tok) WARN_UNUSED_RET {
        if (current == '0') {
            next_char();
            if (!current || (
                ((current | 32) != 'x') && ((current | 32) != 'b') &&
                !(current >= '0' && current <= '7')
            )) {
                /* special case: value 0 */
                tok.value.i = 0;
                tok.numtag = ast::c_expr_type::INT;
                return true;
            }
            if ((current | 32) == 'x') {
                /* hex */
                next_char();
                if (!is_hex_digit(current)) {
                    p_P->ls_buf.set("malformed integer");
                    return lex_error(TOK_INTEGER);
                }
                return read_int_core<16>(is_hex_digit, [](int dig) {
                    dig |= 32;
                    dig = (dig >= 'a') ? (dig - 'a' + 10) : (dig - '0');
                    return dig;
                }, tok);
            } else if ((current | 32) == 'b') {
                /* binary */
                next_char();
                if ((current != '0') && (current != '1')) {
                    p_P->ls_buf.set("malformed integer");
                    return lex_error(TOK_INTEGER);
                }
                return read_int_core<2>([](int cur) {
                    return (cur == '0') || (cur == '1');
                }, [](int dig) {
                    return (dig - '0');
                }, tok);
            } else {
                /* octal */
                return read_int_core<8>([](int cur) {
                    return (cur >= '0') && (cur <= '7');
                }, [](int dig) {
                    return (dig - '0');
                }, tok);
            }
        }
        /* decimal */
        return read_int_core<10>(is_digit, [](int dig) {
            return (dig - '0');
        }, tok);
    }

    bool read_escape(char &c) WARN_UNUSED_RET {
        next_char();
        switch (current) {
            case '\0':
                p_P->ls_buf.set("unterminated escape sequence");
                return lex_error(TOK_CHAR);
            case '\'':
            case '\"':
            case '\\':
            case '?':
                c = char(current);
                next_char();
                return true;
            case 'e': /* extension */
                c = 0x1B;
                next_char();
                return true;
            case 'a': c = '\a'; next_char(); return true;
            case 'b': c = '\b'; next_char(); return true;
            case 'f': c = '\f'; next_char(); return true;
            case 'n': c = '\n'; next_char(); return true;
            case 'r': c = '\r'; next_char(); return true;
            case 't': c = '\t'; next_char(); return true;
            case 'v': c = '\v'; next_char(); return true;
            case 'x': {
                next_char();
                int c1 = current, c2 = upcoming();
                if (!is_hex_digit(c1) || !is_hex_digit(c2)) {
                    p_P->ls_buf.set("malformed hex escape");
                    return lex_error(TOK_CHAR);
                }
                c1 |= 32; c2 |= 32;
                c1 = (c1 >= 'a') ? (c1 - 'a' + 10) : (c1 - '0');
                c2 = (c2 >= 'a') ? (c2 - 'a' + 10) : (c2 - '0');
                c = char(c2 + (c1 * 16));
                next_char();
                next_char();
                return true;
            }
            default:
                break;
        }
        if ((current >= '0') && (current <= '7')) {
            int c1 = current - '0';
            next_char();
            if ((current >= '0') && (current <= '7')) {
                /* 2 or more octal digits */
                int c2 = current - '0';
                next_char();
                if ((current >= '0') && (current <= '7')) {
                    /* 3 octal digits, may be more than 255 */
                    int c3 = current - '0';
                    next_char();
                    int r = (c3 + (c2 * 8) + (c1 * 64));
                    if (r > 0xFF) {
                        p_P->ls_buf.set("octal escape out of bounds");
                        return lex_error(TOK_CHAR);
                    }
                    c = char(r);
                    return true;
                } else {
                    /* 2 octal digits */
                    c = char(c2 + (c1 * 8));
                    return true;
                }
            } else {
                /* 1 octal digit */
                c = char(c1);
                return true;
            }
        }
        p_P->ls_buf.set("malformed escape sequence");
        return lex_error(TOK_CHAR);
    }

    int lex(lex_token &tok) WARN_UNUSED_RET {
        for (;;) switch (current) {
            case '\0':
                return -1;
            case '\n':
            case '\r':
                next_line();
                continue;
            /* either comment or / */
            case '/': {
                next_char();
                if (current == '*') {
                    next_char();
                    while (current) {
                        if (current == '*') {
                            next_char();
                            if (current == '/') {
                                next_char();
                                goto cont;
                            }
                        }
                        next_char();
                    }
                    p_P->ls_buf.set("unterminated comment");
                    return int(syntax_error());
                } else if (current != '/') {
                    /* just / */
                    return '/';
                }
                /* C++ style comment */
                next_char();
                while (current && !is_newline(current)) {
                    next_char();
                }
cont:
                continue;
            }
            /* =, == */
            case '=':
                next_char();
                if (current == '=') {
                    next_char();
                    return TOK_EQ;
                }
                return '=';
            /* !, != */
            case '!':
                next_char();
                if (current == '=') {
                    next_char();
                    return TOK_NEQ;
                }
                return '!';
            /* >, >>, >= */
            case '>':
                next_char();
                if (current == '>') {
                    next_char();
                    return TOK_RSH;
                } else if (current == '=') {
                    next_char();
                    return TOK_GE;
                }
                return '>';
            /* <, <<, <= */
            case '<':
                next_char();
                if (current == '<') {
                    next_char();
                    return TOK_LSH;
                } else if (current == '=') {
                    next_char();
                    return TOK_LE;
                }
                return '<';
            /* &, &&, |, || */
            case '&':
            case '|': {
                int c = current;
                next_char();
                if (current != c) {
                    return c;
                }
                return (c == '&') ? TOK_AND : TOK_OR;
            }
            /* ., ... */
            case '.': {
                next_char();
                if ((current != '.') || (upcoming() != '.')) {
                    return '.';
                }
                next_char();
                next_char();
                return TOK_ELLIPSIS;
            }
            /* (, (( */
            case '(': {
                next_char();
                if ((p_mode == PARSE_MODE_ATTRIB) && (current == '(')) {
                    next_char();
                    return TOK_ATTRIBB;
                }
                return '(';
            }
            /* ), )) */
            case ')': {
                next_char();
                if ((p_mode == PARSE_MODE_ATTRIB) && (current == ')')) {
                    next_char();
                    return TOK_ATTRIBE;
                }
                return ')';
            }
            /* -, -> */
            case '-': {
                next_char();
                if (current == '>') {
                    next_char();
                    return TOK_ARROW;
                }
                return '-';
            }
            /* character literal */
            case '\'': {
                next_char();
                if (current == '\0') {
                    p_P->ls_buf.set("unterminated literal");
                    return int(lex_error(TOK_CHAR));
                } else if (current == '\\') {
                    if (!read_escape(tok.value.c)) {
                        return 0;
                    }
                } else {
                    tok.value.c = char(current);
                    next_char();
                }
                if (current != '\'') {
                    p_P->ls_buf.set("unterminated literal");
                    return int(lex_error(TOK_CHAR));
                }
                next_char();
                tok.numtag = ast::c_expr_type::CHAR;
                return TOK_CHAR;
            }
            /* string literal */
            case '\"': {
                auto &lb = p_P->ls_buf.raw();
                lb.clear();
                next_char();
                for (;;) {
                    if (current == '\"') {
                        /* multiple string literals are one string */
                        if (upcoming() == '\"') {
                            next_char();
                            next_char();
                        } else {
                            break;
                        }
                    }
                    if (current == '\0') {
                        p_P->ls_buf.set("unterminated string");
                        return int(lex_error(TOK_STRING));
                    }
                    if (current == '\\') {
                        char c = '\0';
                        if (!read_escape(c)) {
                            return 0;
                        }
                        lb.push_back(c);
                    } else {
                        lb.push_back(char(current));
                        next_char();
                    }
                }
                next_char();
                lb.push_back('\0');
                return TOK_STRING;
            }
            /* single-char tokens, number literals, keywords, names */
            default: {
                if (is_space(current)) {
                    next_char();
                    continue;
                } else if (is_digit(current)) {
                    if (!read_integer(tok)) {
                        return 0;
                    }
                    return TOK_INTEGER;
                }
                if (is_alpha(current) || (current == '_')) {
                    /* names, keywords */
                    /* what current pointed to */
                    /* keep reading until we readh non-matching char */
                    auto &lb = p_P->ls_buf.raw();
                    lb.clear();
                    do {
                        lb.push_back(next_char());
                    } while (is_alphanum(current) || (current == '_'));
                    lb.push_back('\0');
                    /* could be a keyword? */
                    auto kwit = p_P->keyword_map.find(p_P->ls_buf.data());
                    if (kwit) {
                        return TOK_NAME + *kwit;
                    }
                    return TOK_NAME;
                }
                /* single-char token */
                int c = current;
                next_char();
                return c;
            }
        }
    }

    int current = -1;
    int p_mode = PARSE_MODE_DEFAULT;
    int p_pidx;

    lua_State *p_L;
    parser_state *p_P;
    char const *stream;
    char const *send;

    ast::decl_store p_dstore;

public:
    int line_number = 1;
    lex_token t, lahead;
};

static char const *token_to_str(int tok, char *buf) {
    if (tok < 0) {
        return "<eof>";
    }
    if (tok < TOK_CUSTOM) {
        if (is_print(tok)) {
            buf[0] = char(tok);
            buf[1] = '\0';
        } else {
            char *bufp = buf;
            std::memcpy(bufp, "char(", 5);
            bufp += 5;
            bufp += util::write_i(bufp, 11, tok);
            *bufp++ = ')';
            *bufp++ = '\0';
        }
        return buf;
    }
    return tokens[tok - TOK_CUSTOM];
}

/* parser */

static bool error_expected(lex_state &ls, int tok) WARN_UNUSED_RET;
static bool test_next(lex_state &ls, int tok) WARN_UNUSED_RET;
static bool check(lex_state &ls, int tok) WARN_UNUSED_RET;
static bool check_next(lex_state &ls, int tok) WARN_UNUSED_RET;
static bool check_match(lex_state &ls, int what, int who, int where) WARN_UNUSED_RET;

static bool error_expected(lex_state &ls, int tok) {
    char buf[16 + sizeof("'' expected")];
    char *bufp = buf;
    *bufp++ = '\'';
    char const *tk = token_to_str(tok, bufp);
    auto tlen = std::strlen(tk);
    if (tk != bufp) {
        std::memcpy(bufp, tk, tlen);
    }
    bufp += tlen;
    std::memcpy(bufp, "' expected", sizeof("' expected"));
    ls.get_buf().set(buf);
    return ls.syntax_error();
}

static bool test_next(lex_state &ls, int tok) {
    if (ls.t.token == tok) {
        return ls.get();
    }
    return false;
}

static bool check(lex_state &ls, int tok) {
    if (ls.t.token != tok) {
        return error_expected(ls, tok);
    }
    return true;
}

static bool check_next(lex_state &ls, int tok) {
    if (!check(ls, tok)) {
        return false;
    }
    return ls.get();
}

static bool check_match(lex_state &ls, int what, int who, int where) {
    if (test_next(ls, what)) {
        return true;
    }
    if (where == ls.line_number) {
        return error_expected(ls, what);
    }
    char buf[16];
    auto &b = ls.get_buf();
    b.clear();
    b.append('\'');
    b.append(token_to_str(what, buf));
    b.append("' expected (to close '");
    b.append(token_to_str(who, buf));
    b.append("' at line ");
    util::write_i(buf, sizeof(buf), where);
    b.append(buf);
    b.append(')');
    return ls.syntax_error();
}

static ast::c_expr_binop get_binop(int tok) {
    switch (tok) {
        case '+': return ast::c_expr_binop::ADD;
        case '-': return ast::c_expr_binop::SUB;
        case '*': return ast::c_expr_binop::MUL;
        case '/': return ast::c_expr_binop::DIV;
        case '%': return ast::c_expr_binop::MOD;

        case TOK_EQ:  return ast::c_expr_binop::EQ;
        case TOK_NEQ: return ast::c_expr_binop::NEQ;
        case '>':     return ast::c_expr_binop::GT;
        case '<':     return ast::c_expr_binop::LT;
        case TOK_GE:  return ast::c_expr_binop::GE;
        case TOK_LE:  return ast::c_expr_binop::LE;

        case TOK_AND: return ast::c_expr_binop::AND;
        case TOK_OR:  return ast::c_expr_binop::OR;

        case '&':     return ast::c_expr_binop::BAND;
        case '|':     return ast::c_expr_binop::BOR;
        case '^':     return ast::c_expr_binop::BXOR;
        case TOK_LSH: return ast::c_expr_binop::LSH;
        case TOK_RSH: return ast::c_expr_binop::RSH;

        default: return ast::c_expr_binop::INVALID;
    }
}

static ast::c_expr_unop get_unop(int tok) {
    switch (tok) {
        case '+': return ast::c_expr_unop::UNP;
        case '-': return ast::c_expr_unop::UNM;
        case '!': return ast::c_expr_unop::NOT;
        case '~': return ast::c_expr_unop::BNOT;

        default: return ast::c_expr_unop::INVALID;
    }
}

/* operator precedences as defined by the C standard, as ugly as that is... */

/* matches layout of c_expr_binop */
static constexpr int binprec[] = {
    -1, // invalid

    10,  // +
    10,  // -
    11, // *
    11, // /
    11, // %

    7,  // ==
    7,  // !=
    8,  // >
    8,  // <
    8,  // >=
    8,  // <=

    3,  // &&
    2,  // ||

    6,  // &
    4,  // |
    5,  // ^
    9,  // <<
    9,  // >>
};

static constexpr int unprec = 11;
static constexpr int ifprec = 1;

static bool parse_cexpr(lex_state &ls, ast::c_expr &ret);
static bool parse_cexpr_bin(lex_state &ls, int min_prec, ast::c_expr &ret);

static bool parse_type(
    lex_state &ls, ast::c_type &ret, util::strbuf *fpname = nullptr
);

static ast::c_record const *parse_record(lex_state &ls, bool *newst = nullptr);
static ast::c_enum const *parse_enum(lex_state &ls);

static bool parse_cexpr_simple(lex_state &ls, ast::c_expr &ret) {
    auto unop = get_unop(ls.t.token);
    if (unop != ast::c_expr_unop::INVALID) {
        ast::c_expr exp;
        if (!ls.get() || !parse_cexpr_bin(ls, unprec, exp)) {
            return false;
        }
        ret.type(ast::c_expr_type::UNARY);
        ret.un.op = unop;
        ret.un.expr = new ast::c_expr{util::move(exp)};
        return true;
    }
    /* FIXME: implement non-integer constants */
    if (ls.t.token == '$') {
        if (!ls.param_maybe_expr()) {
            return false;
        }
    }
    switch (ls.t.token) {
        case TOK_INTEGER:
        case TOK_FLOAT:
        case TOK_CHAR: {
            ret.type(ls.t.numtag);
            std::memcpy(&ret.val, &ls.t.value, sizeof(ls.t.value));
            return ls.get();
        }
        case TOK_NAME: {
            auto *o = ls.lookup(ls.get_buf().data());
            if (!o || (o->obj_type() != ast::c_object_type::CONSTANT)) {
                ls.get_buf().prepend("unknown constant '");
                ls.get_buf().append('\'');
                return ls.syntax_error();
            }
            auto &ct = o->as<ast::c_constant>();
            switch (ct.type().type()) {
                case ast::C_BUILTIN_INT:
                    ret.type(ast::c_expr_type::INT); break;
                case ast::C_BUILTIN_UINT:
                    ret.type(ast::c_expr_type::UINT); break;
                case ast::C_BUILTIN_LONG:
                    ret.type(ast::c_expr_type::LONG); break;
                case ast::C_BUILTIN_ULONG:
                    ret.type(ast::c_expr_type::ULONG); break;
                case ast::C_BUILTIN_LLONG:
                    ret.type(ast::c_expr_type::LLONG); break;
                case ast::C_BUILTIN_ULLONG:
                    ret.type(ast::c_expr_type::ULLONG); break;
                case ast::C_BUILTIN_FLOAT:
                    ret.type(ast::c_expr_type::FLOAT); break;
                case ast::C_BUILTIN_DOUBLE:
                    ret.type(ast::c_expr_type::DOUBLE); break;
                case ast::C_BUILTIN_LDOUBLE:
                    ret.type(ast::c_expr_type::LDOUBLE); break;
                case ast::C_BUILTIN_CHAR:
                    ret.type(ast::c_expr_type::CHAR); break;
                case ast::C_BUILTIN_BOOL:
                    ret.type(ast::c_expr_type::BOOL); break;
                default:
                    /* should be generally unreachable */
                    ls.get_buf().set("unknown type");
                    return ls.syntax_error();
            }
            ret.val = ct.value();
            return ls.get();
        }
        case TOK_true:
        case TOK_false: {
            ret.type(ast::c_expr_type::BOOL);
            ret.val.b = (ls.t.token == TOK_true);
            return ls.get();
        }
        case TOK_sizeof: {
            /* TODO: this should also take expressions
             * we just don't support expressions this would support yet
             */
            if (!ls.get()) {
                return false;
            }
            int line = ls.line_number;
            if (!check_next(ls, '(')) {
                return false;
            }
            ast::c_type tp{};
            if (!parse_type(ls, tp) || !check_match(ls, ')', '(', line)) {
                return false;
            }
            auto align = tp.libffi_type()->size;
            if (sizeof(unsigned long long) > sizeof(void *)) {
                ret.type(ast::c_expr_type::ULONG);
                ret.val.ul = static_cast<unsigned long>(align);
            } else {
                ret.type(ast::c_expr_type::ULLONG);
                ret.val.ull = static_cast<unsigned long long>(align);
            }
            return true;
        }
        case TOK_alignof:
        case TOK___alignof__: {
            if (!ls.get()) {
                return false;
            }
            int line = ls.line_number;
            if (!check_next(ls, '(')) {
                return false;
            }
            ast::c_type tp{};
            if (!parse_type(ls, tp) || !check_match(ls, ')', '(', line)) {
                return false;
            }
            auto align = tp.libffi_type()->alignment;
            if (sizeof(unsigned long long) > sizeof(void *)) {
                ret.type(ast::c_expr_type::ULONG);
                ret.val.ul = static_cast<unsigned long>(align);
            } else {
                ret.type(ast::c_expr_type::ULLONG);
                ret.val.ull = static_cast<unsigned long long>(align);
            }
            return true;
        }
        case '(': {
            int line = ls.line_number;
            return ls.get() && parse_cexpr(ls, ret) &&
                check_match(ls, ')', '(', line);
        }
        default:
            break;
    }
    ls.get_buf().set("unexpected symbol");
    return ls.syntax_error();
}

static bool parse_cexpr_bin(lex_state &ls, int min_prec, ast::c_expr &lhs) {
    if (!parse_cexpr_simple(ls, lhs)) {
        return false;
    }
    for (;;) {
        bool istern = (ls.t.token == '?');
        ast::c_expr_binop op{};
        int prec;
        if (istern) {
            prec = ifprec;
        } else {
            op = get_binop(ls.t.token);
            prec = binprec[int(op)];
        }
        /* also matches when prec == -1 (for ast::c_expr_binop::INVALID) */
        if (prec < min_prec) {
            break;
        }
        if (!ls.get()) {
            return false;
        }
        if (istern) {
            ast::c_expr texp;
            if (!parse_cexpr(ls, texp)) {
                return false;
            }
            ast::c_expr fexp;
            if (!check_next(ls, ':') || !parse_cexpr_bin(ls, ifprec, fexp)) {
                return false;
            }
            ast::c_expr tern;
            tern.type(ast::c_expr_type::TERNARY);
            tern.tern.cond = new ast::c_expr{util::move(lhs)};
            tern.tern.texpr = new ast::c_expr{util::move(texp)};
            tern.tern.fexpr = new ast::c_expr{util::move(fexp)};
            lhs = util::move(tern);
            continue;
        }
        /* for right associative this would be prec, we don't
         * have those except ternary which is handled specially
         */
        int nprec = prec + 1;
        ast::c_expr rhs;
        if (!parse_cexpr_bin(ls, nprec, rhs)) {
            return false;
        }
        ast::c_expr bin;
        bin.type(ast::c_expr_type::BINARY);
        bin.bin.op = op;
        bin.bin.lhs = new ast::c_expr{util::move(lhs)};
        bin.bin.rhs = new ast::c_expr{util::move(rhs)};
        lhs = util::move(bin);
    }
    return true;
}

static bool parse_cexpr(lex_state &ls, ast::c_expr &ret) {
    return parse_cexpr_bin(ls, 1, ret);
}

static bool get_arrsize(
    lex_state &ls, ast::c_expr const &exp, std::size_t &ret
) {
    ast::c_expr_type et;
    ast::c_value val;
    if (!exp.eval(ls.lua_state(), val, et, true)) {
        std::size_t strl;
        char const *errm = lua_tolstring(ls.lua_state(), -1, &strl);
        ls.get_buf().set(errm, strl);
        lua_pop(ls.lua_state(), 1);
        return ls.syntax_error();
    }

    long long sval = 0;
    unsigned long long uval = 0;
    switch (et) {
        case ast::c_expr_type::INT: sval = val.i; break;
        case ast::c_expr_type::LONG: sval = val.l; break;
        case ast::c_expr_type::LLONG: sval = val.ll; break;
        case ast::c_expr_type::UINT: uval = val.u; goto done;
        case ast::c_expr_type::ULONG: uval = val.ul; goto done;
        case ast::c_expr_type::ULLONG: uval = val.ull; goto done;
        default:
            ls.get_buf().set("invalid array size");
            return ls.syntax_error();
    }
    if (sval < 0) {
        ls.get_buf().set("array size is negative");
        return ls.syntax_error();
    }
    uval = sval;

done:
    using ULL = unsigned long long;
    if (uval > ULL(~std::size_t(0))) {
        ls.get_buf().set("array sie too big");
        return ls.syntax_error();
    }
    ret = std::size_t(uval);
    return true;
}

static bool parse_cv(
    lex_state &ls, std::uint32_t &ret,
    bool *tdef = nullptr, bool *extr = nullptr
) {
    ret = 0;
    for (;;) switch (ls.t.token) {
        case TOK_const:
        case TOK___const__:
            if (ret & ast::C_CV_CONST) {
                ls.get_buf().set("duplicate const qualifier");
                return ls.syntax_error();
            }
            if (!ls.get()) {
                return false;
            }
            ret |= ast::C_CV_CONST;
            break;
        case TOK_volatile:
        case TOK___volatile__:
            if (ret & ast::C_CV_VOLATILE) {
                ls.get_buf().set("duplicate volatile qualifier");
                return ls.syntax_error();
            }
            if (!ls.get()) {
                return false;
            }
            ret |= ast::C_CV_VOLATILE;
            break;
        case TOK_typedef:
            if (!tdef) {
                return true;
            }
            if (*tdef) {
                ls.get_buf().set("duplicate typedef qualifier");
                return ls.syntax_error();
            }
            if (!ls.get()) {
                return false;
            }
            *tdef = true;
            break;
        case TOK_extern:
            if (!extr) {
                return true;
            }
            if (*extr) {
                ls.get_buf().set("duplicate extern qualifier");
                return ls.syntax_error();
            }
            if (!ls.get()) {
                return false;
            }
            *extr = true;
            break;
        default:
            goto end;
    }
end:
    return true;
}

static bool parse_callconv_attrib(lex_state &ls, std::uint32_t &ret) {
    if (ls.t.token != TOK___attribute__) {
        ret = ast::C_FUNC_DEFAULT;
        return true;
    }
    int omod = ls.mode(PARSE_MODE_ATTRIB);
    if (!ls.get()) {
        return false;
    }
    int ln = ls.line_number;
    if (!check_next(ls, TOK_ATTRIBB)) {
        return false;
    }
    std::uint32_t conv;
    if (!check(ls, TOK_NAME)) {
        return false;
    }
    auto &b = ls.get_buf();
    if (!std::strcmp(b.data(), "cdecl")) {
        conv = ast::C_FUNC_CDECL;
    } else if (!std::strcmp(b.data(), "fastcall")) {
        conv = ast::C_FUNC_FASTCALL;
    } else if (!std::strcmp(b.data(), "stdcall")) {
        conv = ast::C_FUNC_STDCALL;
    } else if (!std::strcmp(b.data(), "thiscall")) {
        conv = ast::C_FUNC_THISCALL;
    } else {
        b.set("invalid calling convention");
        return ls.syntax_error();
    }
    if (!ls.get() || !check_match(ls, TOK_ATTRIBE, TOK_ATTRIBB, ln)) {
        return false;
    }
    ls.mode(omod);
    ret = conv;
    return true;
}

static bool parse_callconv_ms(lex_state &ls, std::uint32_t &ret) {
    switch (ls.t.token) {
        case TOK___cdecl:
            ret = ast::C_FUNC_CDECL;
            return ls.get();
        case TOK___fastcall:
            ret = ast::C_FUNC_FASTCALL;
            return ls.get();
        case TOK___stdcall:
            ret = ast::C_FUNC_STDCALL;
            return ls.get();
        case TOK___thiscall:
            ret = ast::C_FUNC_THISCALL;
            return ls.get();
        default:
            break;
    }
    ret = ast::C_FUNC_DEFAULT;
    return true;
}

static bool parse_paramlist(lex_state &ls, util::vector<ast::c_param> &params) {
    int linenum = ls.line_number;
    if (!ls.get()) {
        return false;
    }

    if (ls.t.token == TOK_void) {
        int lah = 0;
        if (!ls.lookahead(lah)) {
            return false;
        }
        if (lah == ')') {
            if (!ls.get()) {
                return false;
            }
            goto done_params;
        }
    }

    if (ls.t.token == ')') {
        goto done_params;
    }

    for (;;) {
        if (ls.t.token == TOK_ELLIPSIS) {
            /* varargs, insert a sentinel type (will be dropped) */
            params.emplace_back(util::strbuf{}, ast::c_type{
                ast::C_BUILTIN_VOID, 0
            });
            if (!ls.get()) {
                return false;
            }
            /* varargs ends the arglist */
            break;
        }
        util::strbuf pname{};
        ast::c_type pt{};
        if (!parse_type(ls, pt, &pname)) {
            return false;
        }
        /* check if argument type can be passed by value */
        if (!pt.passable()) {
            auto &b = ls.get_buf();
            b.clear();
            b.append('\'');
            pt.serialize(b);
            b.append("' cannot be passed by value");
            return ls.syntax_error();
        }
        if (pname[0] == '?') {
            pname.clear();
        }
        params.emplace_back(util::move(pname), util::move(pt));
        if (!test_next(ls, ',')) {
            break;
        }
    }

done_params:
    return check_match(ls, ')', '(', linenum);
}

/* FIXME: when in var declarations, all components must be complete */
static bool parse_array(lex_state &ls, std::size_t &ret, int &flags) {
    auto &dimstack = ls.array_dim_stack();
    flags = 0;
    std::size_t ndims = 0;
    if (ls.t.token != '[') {
        ret = ndims;
        return true;
    }
    std::uint32_t cv = 0;
    if (!ls.get() || !parse_cv(ls, cv)) {
        return false;
    }
    if (ls.t.token == ']') {
        flags |= ast::C_TYPE_NOSIZE;
        dimstack.push_back({0, cv});
        ++ndims;
        if (!ls.get()) {
            return false;
        }
    } else if (ls.t.token == '?') {
        /* FIXME: this should only be available in cdata creation contexts */
        flags |= ast::C_TYPE_VLA;
        dimstack.push_back({0, cv});
        ++ndims;
        if (!ls.get() || !check_next(ls, ']')) {
            return false;
        }
    } else {
        ast::c_expr exp;
        if (!parse_cexpr(ls, exp)) {
            return false;
        }
        std::size_t arrs;
        if (!get_arrsize(ls, util::move(exp), arrs)) {
            return false;
        }
        dimstack.push_back({arrs, cv});
        ++ndims;
        if (!check_next(ls, ']')) {
            return false;
        }
    }
    while (ls.t.token == '[') {
        if (!ls.get() || !parse_cv(ls, cv)) {
            return false;
        }
        ast::c_expr exp;
        if (!parse_cexpr(ls, exp)) {
            return false;
        }
        std::size_t arrs;
        if (!get_arrsize(ls, util::move(exp), arrs)) {
            return false;
        }
        dimstack.push_back({arrs, cv});
        ++ndims;
        if (!check_next(ls, ']')) {
            return false;
        }
    }
    ret = ndims;
    return true;
}

/* this attempts to implement the complete syntax of how types are parsed
 * in C; that means it covers pointers, function pointers, references
 * and arrays, including hopefully correct parenthesization rules and
 * binding of pointers/references and cv qualifiers...
 *
 * it also handles proper parsing and placement of name, e.g. when declaring
 * function prototypes or dealing with struct members or function arguments,
 * so you could say it handles not only parsing of types by itself, but really
 * parsing of declarations in general; if you look down in parse_decl, you can
 * see that a declaration parse is pretty much just calling parse_type()
 *
 * below is described how it works:
 */
static bool parse_type_ptr(
    lex_state &ls, ast::c_type &tp, util::strbuf *fpname, bool needn,
    bool tdef, bool &tdef_bltin
) {
    /* our input is the left-side qualified type; that means constructs such
     * as 'const int' or 'unsigned long int const'
     */

    /*
     * first we define a list containing 'levels'... each level denotes one
     * matched pair of parens, except the implicit default level which is
     * always added; new level is delimited by a sentinel value, and the
     * elements past the sentinel can specify pointers and references
     *
     * this list is stored in the per-lua-state parser state struct, to avoid
     * the hassle with static thread-local constructors/destructors, while
     * still conserving resources (reused across parser runs)
     *
     * now the real fun begins, because we're going to be statekeeping
     *
     * the C function pointer syntax is quite awful, as is the array syntax
     * that follows similar conventions - let's start with the simplest
     * example of a function declaration:
     *
     * void const *func_name(int arg);
     *
     * that as a whole is a type - there is also a fairly liberal usage of
     * parenthesis allowed, so we can write this identical declaration:
     *
     * void const *(func_name)(int arg);
     *
     * now let's turn it into a function pointer declaration - in that case,
     * the parenthesis becomes mandatory, since we need to bind a pointer to
     * the function and not to the left-side type:
     *
     * void const *(*func_name)(int arg);
     *
     * now, there is this fun rule of how the '*' within the parenthesis binds
     * to something - consider this:
     *
     * void const (*ptr_name);
     *
     * this is functionally identical to:
     *
     * void const *ptr_name;
     *
     * in short, if an argument list follows a parenthesized part of the type,
     * we've created a function type, and the asterisks inside the parens will
     * now bind to the new function type rather than the 'void const *'
     *
     * the same applies to arrays:
     *
     * void const *(*thing);      // pointer to pointer to const void
     * void const *(*thing)[100]; // pointer to array of pointers
     * void const **thing[100];   // array of pointers to pointers
     * void const *(*thing[100]); // same as above
     *
     * now, what if we wanted to nest this, and create a function that returns
     * a function pointer? we put the argument list after the name:
     *
     * void const *(*func_name(int arg))(float other_arg);
     *
     * this is a plain function taking one 'int' argument that returns a
     * pointer to function taking one 'float' argument and returning a ptr
     *
     * what if we wanted to make THIS into a function pointer?
     *
     * void const *(*(*func_name)(int arg))(float other_arg);
     *
     * now we have a pointer to the same thing as above...
     *
     * this can be nested indefinitely, and arrays behave exactly the same;
     * that also means pointers/references bind left to right (leftmost '*'
     * is the 'deepest' pointer, e.g. 'void (*&foo)()' is a reference to a
     * function pointer) while argument lists bind right to left
     *
     * array dimensions for multidimensional arrays also bind right to left,
     * with leftmost dimension being the outermost (which can therefore be of
     * unknown/variable size, while the others must specify exact sizes)
     *
     * cv-qualifiers bind to the thing on their left as usual and can be
     * specified after any '*' (but not '&'); references behave the same
     * way as pointers with the difference that you can't have a reference
     * to a reference, it must terminate the chain
     *
     * this is enough background, let's parse:
     *
     * first we save the size of the level list, that will denote the boundary
     * of this specific call - it will be the first index we can access from
     * here - this is because this function can be recursive, and we can't have
     * inner calls overwriting stuff that belongs to the outer calls
     */
    auto &pcvq = ls.type_level_queue();
    auto pidx = std::intptr_t(pcvq.size());
    bool nolev = true;
    /* normally we'd consume the '(', but remember, first level is implicit */
    goto newlevel;
    do {
        if (!ls.get()) {
            return false;
        }
        nolev = false;
newlevel:
        /* create the sentinel */
        pcvq.emplace_back();
        pcvq.back().is_term = true;
        if (!nolev) {
            std::uint32_t conv = 0;
            if (!parse_callconv_ms(ls, conv)) {
                return false;
            }
            pcvq.back().cconv = conv;
        } else {
            pcvq.back().cconv = ast::C_FUNC_DEFAULT;
        }
        /* count all '*' and create element for each */
        while (ls.t.token == '*') {
            pcvq.emplace_back();
            std::uint32_t cv = 0;
            if (!ls.get() || !parse_cv(ls, cv)) {
                return false;
            }
            pcvq.back().cv = cv;
        }
        /* references are handled the same, but we know there can only be
         * one of them; this actually does not cover all cases, since putting
         * parenthesis after this will allow you to specify another reference,
         * but filter this trivial case early on since we can */
        if (ls.t.token == '&') {
            if (!ls.get()) {
                return false;
            }
            pcvq.emplace_back();
            pcvq.back().is_ref = true;
        }
        /* we've found what might be an array dimension or an argument list,
         * so break out, we've finished parsing the early bits...
         */
        if (ls.t.token == '[') {
            break;
        } else if (ls.t.token == '(') {
            /* these indicate not an arglist, so keep going */
            int lah = 0;
            if (!ls.lookahead(lah)) {
                return false;
            }
            switch (lah) {
                case TOK___cdecl:
                case TOK___fastcall:
                case TOK___stdcall:
                case TOK___thiscall:
                case '*':
                case '&':
                case '(':
                    continue;
                default:
                    break;
            }
            break;
        }
    } while (ls.t.token == '(');
    /* this function doesn't change the list past this, so save it */
    auto tidx = std::intptr_t(pcvq.size());
    /* the most basic special case when there are no (),
     * calling convention can go before the name
     */
    if (nolev) {
        std::uint32_t conv = 0;
        if (!parse_callconv_ms(ls, conv)) {
            return false;
        }
        pcvq[pidx].cconv = conv;
        if (pcvq[pidx].cconv == ast::C_FUNC_DEFAULT) {
            if (!parse_callconv_attrib(ls, conv)) {
                return false;
            }
            pcvq[pidx].cconv = conv;
        }
    }
    /* if 'fpname' was passed, it means we might want to handle a named type
     * or declaration, with the name being optional or mandatory depending
     * on 'needn' - if name was optionally requested but not found, we write
     * a dummy value to tell the caller what happened
     */
    if (fpname) {
        if (!ls.param_maybe_name()) {
            return false;
        }
        bool check_kw = (ls.t.token == TOK_NAME);
        if (tdef) {
            /* builtins can be "redefined", but the definitions
             * are ignored, matching luajit fuzzy behavior
             */
            switch (ls.t.token) {
                case TOK_int8_t:
                case TOK_int16_t:
                case TOK_int32_t:
                case TOK_int64_t:
                case TOK_uint8_t:
                case TOK_uint16_t:
                case TOK_uint32_t:
                case TOK_uint64_t:
                case TOK_uintptr_t:
                case TOK_intptr_t:
                case TOK_ptrdiff_t:
                case TOK_ssize_t:
                case TOK_size_t:
                case TOK_va_list:
                case TOK___builtin_va_list:
                case TOK___gnuc_va_list:
                case TOK_time_t:
                case TOK_wchar_t:
                    check_kw = true;
                    tdef_bltin = true;
                    break;
                default:
                    break;
            }
        }
        if (needn || check_kw) {
            /* we're in a context where name can be provided, e.g. if
             * parsing a typedef or a prototype, this will be the name
             */
            if (!check_kw && !check(ls, TOK_NAME)) {
                return false;
            }
            *fpname = ls.get_buf();
            if (!ls.get()) {
                return false;
            }
        } else {
            fpname->set("?");
        }
    }
    /* remember when we declared that paramlists and array dimensions bind
     * right to left, rather than left to right? we now have a queue of levels
     * available, and we might be at the first, innermost argument list, or
     * maybe an array; what we do is iterate all sentinels in the queue, but
     * backwards, starting with the last defined one...
     *
     * once we've found one, we attempt to parse an argument list or an array
     * dimension depending on what was found, and if nothing was found, that
     * is fine too - this will alter to what pointers are bound to though
     *
     * in short, in 'void (*foo(argl1))(argl2)', 'argl1' will be attached to
     * level 2, while 'argl2' will be stored in level 1 (the implicit one)
     */
    std::uint32_t prevconv = ast::C_FUNC_DEFAULT;
    for (std::intptr_t ridx = tidx - 1;;) {
        if (!pcvq[ridx].is_term) { /* skip non-sentinels */
            --ridx;
            continue;
        }
        if (ls.t.token == '(') {
            /* we know it's a paramlist, since all starting '(' of levels
             * are already consumed since before
             */
            util::vector<ast::c_param> argl{};
            if (!parse_paramlist(ls, argl)) {
                return false;
            }
            auto &clev = pcvq[ridx];
            new (&clev.argl) util::vector<ast::c_param>(util::move(argl));
            clev.is_func = true;
            /* attribute style calling convention after paramlist */
            std::uint32_t conv = 0;
            if (!parse_callconv_attrib(ls, conv)) {
                return false;
            }
            clev.cconv = conv;
            if (clev.cconv == ast::C_FUNC_DEFAULT) {
                clev.cconv = prevconv;
            }
        } else if (ls.t.token == '[') {
            /* array dimensions may be multiple */
            int flags = 0;
            std::size_t arrd = 0;
            if (!parse_array(ls, arrd, flags)) {
                return false;
            }
            pcvq[ridx].arrd = arrd;
            pcvq[ridx].flags = flags;
        }
        if (!pcvq[ridx].is_func && (prevconv != ast::C_FUNC_DEFAULT)) {
            ls.get_buf().set("calling convention on non-function declaration");
            return ls.syntax_error();
        }
        prevconv = pcvq[ridx].cconv;
        --ridx;
        /* special case of the implicit level, it's not present in syntax */
        if (ridx < pidx) {
            break;
        }
        if (!check_next(ls, ')')) {
            return false;
        }
    }
    /* now that arglists and arrays are attached to their respective levels,
     * we can iterate the queue forward, and execute the appropriate binding
     * logic, which is as follows:
     *
     * we take a pointer to the 'outer' level, which is by default the implicit
     * one, and start iterating from the second level afterwards; in something
     * like a function pointer or something parenthesized, this may be another
     * level already, or it might be a bunch of pointer declarations...
     *
     * let's consider our previous, moderately complex example:
     *
     * void const *(*(*func_name)(int arg))(float other_arg);
     *
     * right now, 'tp' is 'void const *', and the first outer level is the
     * implicit one, and it has the float arglist attached to it... so, we
     * start at level 2, and do this:
     *
     * void const * -> function<void const *, (float other_arg)>
     *
     * now, our level 2 has one '*'; this binds to whatever is currently 'tp',
     * so as a result, we get:
     *
     * function<void const *, (float other_arg)> *
     *
     * and that's all, so we set the 'outer' level to level 2 and proceed to
     * level 3...
     *
     * our new 'outer' level has the int arglist, so we do this:
     *
     * function<...> * -> function<function<...>, (int arg)>
     *
     * and finally bind level 3's '*' to it, which gets us the final type,
     * which is a pointer to a function returning a pointer to a function.
     */
    parser_type_level *olev = &pcvq[pidx];
    auto &dimstack = ls.array_dim_stack();
    for (std::intptr_t cidx = pidx + 1;; ++cidx) {
        /* for the implicit level, its pointers/ref are bound to current 'tp',
         * as there is definitely no outer arglist or anything, and we need
         * to make sure return types for functions are properly built, e.g.
         *
         * void *(*foo)()
         *
         * here 'tp' is 'void' at first, we need to make it into a 'void *'
         * before proceeding to 2nd level, which will then attach the arglist
         *
         * for any further level, bind pointers and reférences to whatever is
         * 'tp' at the time, which will be a new thing if an arglist is there,
         * or the previous type if not
         */
        while ((cidx < tidx) && !pcvq[cidx].is_term) {
            /* references are trailing, we can't make pointers
             * to them nor we can make references to references
             */
            if (tp.is_ref()) {
                ls.get_buf().set("references must be trailing");
                return ls.syntax_error();
            }
            if (pcvq[cidx].is_ref) {
                tp.add_ref();
            } else {
                ast::c_type ntp{
                    util::make_rc<ast::c_type>(util::move(tp)),
                    pcvq[cidx].cv, ast::C_BUILTIN_PTR
                };
                tp = util::move(ntp);
            }
            ++cidx;
        }
        /* now attach the function or array or whatever */
        if (olev->is_func) {
            /* outer level has an arglist */
            std::uint32_t fflags = olev->cconv;
            if (!olev->argl.empty() && (
                olev->argl.back().type().type() == ast::C_BUILTIN_VOID
            )) {
                fflags |= ast::C_FUNC_VARIADIC;
                olev->argl.pop_back();
            }
            /* check if return type can be passed */
            if (
                (tp.type() == ast::C_BUILTIN_ARRAY) ||
                ((tp.type() != ast::C_BUILTIN_VOID) && !tp.passable())
            ) {
                auto &b = ls.get_buf();
                b.clear();
                b.append('\'');
                tp.serialize(b);
                b.append("' cannot be passed by value");
                return ls.syntax_error();
                break;
            }
            tp = ast::c_type{util::make_rc<ast::c_function>(
                util::move(tp), util::move(olev->argl), fflags
            ), 0, false};
        } else if (olev->arrd) {
            if (tp.flex()) {
                ls.get_buf().set(
                    "only first bound of an array may have unknown size"
                );
                return ls.syntax_error();
            }
            while (olev->arrd) {
                std::size_t dim = dimstack.back().size;
                auto quals = dimstack.back().quals;
                dimstack.pop_back();
                --olev->arrd;
                ast::c_type atp{
                    util::make_rc<ast::c_type>(util::move(tp)),
                    quals, dim, (!olev->arrd ? olev->flags : std::uint32_t(0))
                };
                tp = util::move(atp);
            }
        }
        if (cidx >= tidx) {
            break;
        }
        olev = &pcvq[cidx];
    }
    /* one last thing: if plain void type is not allowed in this context
     * and we nevertheless got it, we need to error
     */
    if (
        (ls.mode() == PARSE_MODE_DEFAULT) && (tp.type() == ast::C_BUILTIN_VOID)
    ) {
        ls.get_buf().set("void type in forbidden context");
        return ls.syntax_error();
    }
    /* shrink it back to what it was, these resources can be reused later */
    pcvq.shrink(pidx);
    return true;
}

enum type_signedness {
    TYPE_SIGNED = 1 << 0,
    TYPE_UNSIGNED = 1 << 1
};

using signed_size_t = util::conditional_t<
    sizeof(std::size_t) == sizeof(char), signed char,
    util::conditional_t<
        sizeof(std::size_t) == sizeof(short), short,
        util::conditional_t<
            sizeof(std::size_t) == sizeof(int), int,
            util::conditional_t<
                sizeof(std::size_t) == sizeof(long), long, long long
            >
        >
    >
>;

static bool parse_typebase_core(
    lex_state &ls, ast::c_type &ret, bool *tdef, bool *extr
) {
    /* left-side cv */
    std::uint32_t quals = 0;
    if (!parse_cv(ls, quals, tdef, extr)) {
        return false;
    }
    std::uint32_t squals = 0;

    /* parameterized types */
    if (ls.t.token == '$') {
        if (!ls.param_get_type(ret)) {
            return false;
        }
        ret.cv(quals);
        return true;
    }

    ast::c_builtin cbt = ast::C_BUILTIN_INVALID;

    if (ls.t.token == TOK_signed || ls.t.token == TOK_unsigned) {
        if (ls.t.token == TOK_signed) {
            squals |= TYPE_SIGNED;
        } else {
            squals |= TYPE_UNSIGNED;
        }
        if (!ls.get()) {
            return false;
        }
        /* when followed by char/short/int/long, it means signed/unsigned
         * was used as a mere qualifier, so proceed with parsing the type
         */
        switch (ls.t.token) {
            case TOK_char:
            case TOK_short:
            case TOK_int:
            case TOK_long:
                goto qualified;
            default:
                break;
        }
        /* when not followed by that, treat them as a whole type */
        if (squals & TYPE_SIGNED) {
            cbt = ast::C_BUILTIN_INT;
        } else {
            cbt = ast::C_BUILTIN_UINT;
        }
        goto newtype;
    } else if ((ls.t.token == TOK_struct) || (ls.t.token == TOK_union)) {
        auto *st = parse_record(ls);
        if (!st) {
            return false;
        }
        ret = ast::c_type{st, quals};
        return true;
    } else if (ls.t.token == TOK_enum) {
        auto *en = parse_enum(ls);
        if (!en) {
            return false;
        }
        ret = ast::c_type{en, quals};
        return true;
    }

qualified:
    if (ls.t.token == TOK_NAME) {
        /* typedef, struct, enum, var, etc. */
        auto *decl = ls.lookup(ls.get_buf().data());
        if (!decl) {
            ls.get_buf().prepend("undeclared symbol '");
            ls.get_buf().append('\'');
            return ls.syntax_error();
        }
        switch (decl->obj_type()) {
            case ast::c_object_type::TYPEDEF: {
                if (!ls.get()) {
                    return false;
                }
                ret = decl->as<ast::c_typedef>().type().copy();
                /* merge qualifiers */
                ret.cv(quals);
                return true;
            }
            case ast::c_object_type::RECORD: {
                if (!ls.get()) {
                    return false;
                }
                auto &tp = decl->as<ast::c_record>();
                ret = ast::c_type{&tp, quals};
                return true;
            }
            case ast::c_object_type::ENUM: {
                if (!ls.get()) {
                    return false;
                }
                auto &tp = decl->as<ast::c_enum>();
                ret = ast::c_type{&tp, quals};
                return true;
            }
            default: {
                ls.get_buf().prepend("symbol '");
                ls.get_buf().append("' is not a type");
                return ls.syntax_error();
            }
        }
    } else switch (ls.t.token) {
        /* may be a builtin type */
        case TOK_void:
            cbt = ast::C_BUILTIN_VOID;
            goto btype;
        case TOK_int8_t:
            cbt = ast::builtin_v<std::int8_t>;
            goto btype;
        case TOK_int16_t:
            cbt = ast::builtin_v<std::int16_t>;
            goto btype;
        case TOK_int32_t:
            cbt = ast::builtin_v<std::int32_t>;
            goto btype;
        case TOK_int64_t:
            cbt = ast::builtin_v<std::int64_t>;
            goto btype;
        case TOK_uint8_t:
            cbt = ast::builtin_v<std::uint8_t>;
            goto btype;
        case TOK_uint16_t:
            cbt = ast::builtin_v<std::uint16_t>;
            goto btype;
        case TOK_uint32_t:
            cbt = ast::builtin_v<std::uint32_t>;
            goto btype;
        case TOK_uint64_t:
            cbt = ast::builtin_v<std::uint64_t>;
            goto btype;
        case TOK_uintptr_t:
            cbt = ast::builtin_v<std::uintptr_t>;
            goto btype;
        case TOK_intptr_t:
            cbt = ast::builtin_v<std::intptr_t>;
            goto btype;
        case TOK_ptrdiff_t:
            cbt = ast::builtin_v<std::ptrdiff_t>;
            goto btype;
        case TOK_ssize_t:
            cbt = ast::builtin_v<signed_size_t>;
            goto btype;
        case TOK_size_t:
            cbt = ast::builtin_v<std::size_t>;
            goto btype;
        case TOK_va_list:
        case TOK___builtin_va_list:
        case TOK___gnuc_va_list:
            cbt = ast::C_BUILTIN_VA_LIST;
            goto btype;
        case TOK_time_t:   cbt = ast::builtin_v<std::time_t>; goto btype;
        case TOK_wchar_t:  cbt = ast::builtin_v<wchar_t>;  goto btype;
        case TOK_char16_t: cbt = ast::builtin_v<char16_t>; goto btype;
        case TOK_char32_t: cbt = ast::builtin_v<char32_t>; goto btype;
        case TOK_float:    cbt = ast::C_BUILTIN_FLOAT;     goto btype;
        case TOK_double:   cbt = ast::C_BUILTIN_DOUBLE;    goto btype;
        case TOK_bool:
        case TOK__Bool:
            cbt = ast::C_BUILTIN_BOOL;
        btype:
            if (!ls.get()) {
                return false;
            }
            break;
        case TOK_char:
            if (squals & TYPE_SIGNED) {
                cbt = ast::C_BUILTIN_SCHAR;
            } else if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_UCHAR;
            } else {
                cbt = ast::C_BUILTIN_CHAR;
            }
            if (!ls.get()) {
                return false;
            }
            break;
        case TOK_short:
            if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_USHORT;
            } else {
                cbt = ast::C_BUILTIN_SHORT;
            }
            if (!ls.get()) {
                return false;
            }
            if ((ls.t.token == TOK_int) && !ls.get()) {
                return false;
            }
            break;
        case TOK_int:
            if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_UINT;
            } else {
                cbt = ast::C_BUILTIN_INT;
            }
            if (!ls.get()) {
                return false;
            }
            break;
        case TOK_long:
            if (!ls.get()) {
                return false;
            }
            if (ls.t.token == TOK_long) {
                if (squals & TYPE_UNSIGNED) {
                    cbt = ast::C_BUILTIN_ULLONG;
                } else {
                    cbt = ast::C_BUILTIN_LLONG;
                }
                if (!ls.get()) {
                    return false;
                }
            } else if (ls.t.token == TOK_int) {
                if (squals & TYPE_UNSIGNED) {
                    cbt = ast::C_BUILTIN_ULONG;
                } else {
                    cbt = ast::C_BUILTIN_LONG;
                }
                if (!ls.get()) {
                    return false;
                }
            } else if (ls.t.token == TOK_double) {
                cbt = ast::C_BUILTIN_LDOUBLE;
                if (!ls.get()) {
                    return false;
                }
            } else if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_ULONG;
            } else {
                cbt = ast::C_BUILTIN_LONG;
            }
            break;
        default:
            ls.get_buf().set("type name expected");
            return ls.syntax_error();
    }

newtype:
    assert(cbt != ast::C_BUILTIN_INVALID);
    ret = ast::c_type{cbt, quals};
    return true;
}

static bool parse_typebase(
    lex_state &ls, ast::c_type &ret, bool *tdef = nullptr, bool *extr = nullptr
) {
    if (!parse_typebase_core(ls, ret, tdef, extr)) {
        return false;
    }
    /* right-side cv that can always apply */
    std::uint32_t cv = 0;
    if (!parse_cv(ls, cv, tdef, extr)) {
        return false;
    }
    ret.cv(cv);
    return true;
}

static bool parse_type(lex_state &ls, ast::c_type &ret, util::strbuf *fpn) {
    bool tdef_bltin = false;
    return (parse_typebase(ls, ret) && parse_type_ptr(
        ls, ret, fpn, false, false, tdef_bltin
    ));
}

static ast::c_record const *parse_record(lex_state &ls, bool *newst) {
    int sline = ls.line_number;
    bool is_uni = (ls.t.token == TOK_union);
    /* struct/union keyword */
    if (!ls.get()) {
        return nullptr;
    }
    /* name is optional */
    bool named = false;
    util::strbuf sname{is_uni ? "union " : "struct "};
    if (!ls.param_maybe_name()) {
        return nullptr;
    }
    if (ls.t.token == TOK_NAME) {
        sname.append(ls.get_buf());
        if (!ls.get()) {
            return nullptr;
        }
        named = true;
    } else {
        char buf[32];
        auto wn = ls.request_name(buf, sizeof(buf));
        static_cast<void>(wn); /* silence NDEBUG warnings */
        assert(wn < sizeof(buf));
        sname.append(buf);
    }

    int linenum = ls.line_number;

    auto mode_error = [&ls, named]() -> bool {
        if (named && (ls.mode() == PARSE_MODE_NOTCDEF)) {
            ls.get_buf().set("struct declaration not allowed in this context");
            return ls.syntax_error();
        }
        return true;
    };

    /* opaque */
    if (!test_next(ls, '{')) {
        auto *oldecl = ls.lookup(sname.data());
        if (!oldecl || (oldecl->obj_type() != ast::c_object_type::RECORD)) {
            if (!mode_error()) {
                return nullptr;
            }
            /* different type or not stored yet, raise error or store */
            auto *p = new ast::c_record{util::move(sname), is_uni};
            if (!ls.store_decl(p, sline)) {
                return nullptr;
            }
            return p;
        }
        return &oldecl->as<ast::c_record>();
    }

    if (!mode_error()) {
        return nullptr;
    }

    util::vector<ast::c_record::field> fields;

    while (ls.t.token != '}') {
        ast::c_type tpb{};
        if ((ls.t.token == TOK_struct) || (ls.t.token == TOK_union)) {
            bool transp = false;
            auto *st = parse_record(ls, &transp);
            if (!st) {
                return nullptr;
            }
            if (transp && test_next(ls, ';')) {
                fields.emplace_back(util::strbuf{}, ast::c_type{st, 0});
                continue;
            }
            std::uint32_t cv = 0;
            if (!parse_cv(ls, cv)) {
                return nullptr;
            }
            tpb = ast::c_type{st, cv};
        } else {
            if (!parse_typebase(ls, tpb)) {
                return nullptr;
            }
        }
        bool flexible = false;
        do {
            util::strbuf fpn;
            auto tp = tpb.copy();
            bool tdef_bltin = false;
            if (!parse_type_ptr(ls, tp, &fpn, false, false, tdef_bltin)) {
                return nullptr;
            }
            if (fpn[0] == '?') {
                /* nameless field declarations do nothing */
                goto field_end;
            }
            flexible = tp.flex();
            fields.emplace_back(util::move(fpn), util::move(tp));
            /* flexible array must be the last in the list */
            if (flexible) {
                break;
            }
        } while (test_next(ls, ','));
field_end:
        if (!check_next(ls, ';')) {
            return nullptr;
        }
        /* flexible array must be the last in the struct */
        if (flexible) {
            break;
        }
    }

    if (!check_match(ls, '}', '{', linenum)) {
        return nullptr;
    }

    auto *oldecl = ls.lookup(sname.data());
    if (oldecl && (oldecl->obj_type() == ast::c_object_type::RECORD)) {
        auto &st = oldecl->as<ast::c_record>();
        if (st.opaque()) {
            /* previous declaration was opaque; prevent redef errors */
            st.set_fields(util::move(fields));
            if (newst) {
                *newst = true;
            }
            return &st;
        }
    }

    if (newst) {
        *newst = true;
    }
    auto *p = new ast::c_record{util::move(sname), util::move(fields), is_uni};
    if (!ls.store_decl(p, sline)) {
        return nullptr;
    }
    return p;
}

static ast::c_enum const *parse_enum(lex_state &ls) {
    int eline = ls.line_number;
    if (!ls.get()) {
        return nullptr;
    }
    /* name is optional */
    bool named = false;
    util::strbuf ename{"enum "};
    if (!ls.param_maybe_name()) {
        return nullptr;
    }
    if (ls.t.token == TOK_NAME) {
        ename.append(ls.get_buf());
        if (!ls.get()) {
            return nullptr;
        }
        named = true;
    } else {
        char buf[32];
        auto wn = ls.request_name(buf, sizeof(buf));
        static_cast<void>(wn); /* silence NDEBUG warnings */
        assert(wn < sizeof(buf));
        ename.append(buf);
    }

    int linenum = ls.line_number;

    auto mode_error = [&ls, named]() -> bool {
        if (named && (ls.mode() == PARSE_MODE_NOTCDEF)) {
            ls.get_buf().set("enum declaration not allowed in this context");
            return ls.syntax_error();
        }
        return true;
    };

    if (!test_next(ls, '{')) {
        auto *oldecl = ls.lookup(ename.data());
        if (!oldecl || (oldecl->obj_type() != ast::c_object_type::ENUM)) {
            if (!mode_error()) {
                return nullptr;
            }
            auto *p = new ast::c_enum{util::move(ename)};
            if (!ls.store_decl(p, eline)) {
                return nullptr;
            }
            return p;
        }
        return &oldecl->as<ast::c_enum>();
    }

    if (!mode_error()) {
        return nullptr;
    }

    util::vector<ast::c_enum::field> fields;

    while (ls.t.token != '}') {
        int eln = ls.line_number;
        if (!ls.param_maybe_name() || !check(ls, TOK_NAME)) {
            return nullptr;
        }
        util::strbuf fname{ls.get_buf()};
        if (!ls.get()) {
            return nullptr;
        }
        if (ls.t.token == '=') {
            eln = ls.line_number;
            ast::c_expr exp;
            if (!ls.get() || !parse_cexpr(ls, exp)) {
                return nullptr;
            }
            ast::c_expr_type et;
            ast::c_value val;
            if (!exp.eval(ls.lua_state(), val, et, true)) {
                std::size_t strl;
                char const *errm = lua_tolstring(ls.lua_state(), -1, &strl);
                ls.get_buf().set(errm, strl);
                lua_pop(ls.lua_state(), 1);
                if (!ls.syntax_error()) {
                    return nullptr;
                }
            }
            /* for now large types just get truncated */
            switch (et) {
                case ast::c_expr_type::INT: break;
                case ast::c_expr_type::UINT: val.i = val.u; break;
                case ast::c_expr_type::LONG: val.i = int(val.l); break;
                case ast::c_expr_type::ULONG: val.i = int(val.ul); break;
                case ast::c_expr_type::LLONG: val.i = int(val.ll); break;
                case ast::c_expr_type::ULLONG: val.i = int(val.ull); break;
                default:
                    ls.get_buf().set("unsupported type");
                    if (!ls.syntax_error()) {
                        return nullptr;
                    }
            }
            fields.emplace_back(util::move(fname), val.i);
        } else {
            fields.emplace_back(
                util::move(fname), fields.empty() ? 0 : (fields.back().value + 1)
            );
        }
        /* enums: register fields as constant values
         * FIXME: don't hardcode like this
         */
        auto &fld = fields.back();
        ast::c_value fval;
        fval.i = fld.value;
        auto *p = new ast::c_constant{
            fld.name, ast::c_type{ast::C_BUILTIN_INT, 0}, fval
        };
        if (!ls.store_decl(p, eln)) {
            return nullptr;
        }
        if (ls.t.token != ',') {
            break;
        } else if (!ls.get()) {
            return nullptr;
        }
    }

    if (!check_match(ls, '}', '{', linenum)) {
        return nullptr;
    }

    auto *oldecl = ls.lookup(ename.data());
    if (oldecl && (oldecl->obj_type() == ast::c_object_type::ENUM)) {
        auto &st = oldecl->as<ast::c_enum>();
        if (st.opaque()) {
            /* previous declaration was opaque; prevent redef errors */
            st.set_fields(util::move(fields));
            return &st;
        }
    }

    auto *p = new ast::c_enum{util::move(ename), util::move(fields)};
    if (!ls.store_decl(p, eline)) {
        return nullptr;
    }
    return p;
}

static bool parse_decl(lex_state &ls) {
    int dline = ls.line_number;
    std::uint32_t cconv = 0;
    if (!parse_callconv_attrib(ls, cconv)) {
        return false;
    }
    bool tdef = false, extr = false;
    ast::c_type tpb{};
    if (!parse_typebase(ls, tpb, &tdef, &extr)) {
        return false;
    }
    bool first = true;
    do {
        util::strbuf dname;
        int oldmode = 0;
        if (tdef) {
            oldmode = ls.mode(PARSE_MODE_TYPEDEF);
        }
        auto tp = tpb.copy();
        bool tdef_bltin = false;
        if (!parse_type_ptr(ls, tp, &dname, !first, tdef, tdef_bltin)) {
            return false;
        }
        first = false;
        if (cconv != ast::C_FUNC_DEFAULT) {
            if (tp.type() != ast::C_BUILTIN_FUNC) {
                ls.get_buf().set(
                    "calling convention on non-function declaration"
                );
                return ls.syntax_error();
            }
            auto *func = const_cast<ast::c_function *>(&*tp.function());
            func->callconv(cconv);
        }
        if (tdef) {
            ls.mode(oldmode);
            if (dname[0] != '?') {
                /* redefinition of builtin, skip */
                if (tdef_bltin) {
                    continue;
                }
                /* store if the name is non-empty, if it's empty there is no
                 * way to access the type and it'd be unique either way
                 */
                if (!ls.store_decl(new ast::c_typedef{
                    util::move(dname), util::move(tp)
                }, dline)) {
                    return false;
                }
                continue;
            } else {
                /* unnamed typedef must not be a list */
                break;
            }
        } else if (dname[0] == '?') {
            /* if no name is permitted, it must be the only one */
            break;
        }
        util::strbuf sym;
        /* symbol redirection */
        if (test_next(ls, TOK___asm__)) {
            int lnum = ls.line_number;
            if (!check_next(ls, '(') || !check(ls, TOK_STRING)) {
                return false;
            }
            if (ls.get_buf().empty()) {
                ls.get_buf().set("empty symbol name");
                return ls.syntax_error();
            }
            sym = ls.get_buf();
            if (!ls.get() || !check_match(ls, ')', '(', lnum)) {
                return false;
            }
        }
        if (!ls.store_decl(new ast::c_variable{
            util::move(dname), util::move(sym), util::move(tp)
        }, dline)) {
            return false;
        }
    } while (test_next(ls, ','));
    return true;
}

static bool parse_decls(lex_state &ls) {
    while (ls.t.token >= 0) {
        if (ls.t.token == ';') {
            /* empty statement */
            if (!ls.get()) {
                return false;
            }
            continue;
        }
        if (!parse_decl(ls)) {
            return false;
        }
        if (!ls.t.token) {
            break;
        }
        if (!check_next(ls, ';')) {
            return false;
        }
    }
    return true;
}

static void parse_err(lua_State *L) {
    luaL_where(L, 1);
    lua_insert(L, -2);
    lua_concat(L, 2);
    lua_error(L);
}

void parse(lua_State *L, char const *input, char const *iend, int paridx) {
    if (!iend) {
        iend = input + std::strlen(input);
    }
    {
        lex_state ls{L, input, iend, PARSE_MODE_DEFAULT, paridx};
        if (!ls.get() || !parse_decls(ls)) {
            if (ls.err_token() > 0) {
                char buf[16];
                lua_pushfstring(
                    L, "input:%d: %s near '%s'", ls.err_line(),
                    ls.get_buf().data(), token_to_str(ls.err_token(), buf)
                );
            } else {
                lua_pushfstring(
                    L, "input:%d: %s", ls.err_line(), ls.get_buf().data()
                );
            }
            goto lerr;
        }
        ls.commit();
        return;
    }
lerr:
    parse_err(L);
}

ast::c_type parse_type(
    lua_State *L, char const *input, char const *iend, int paridx
) {
    if (!iend) {
        iend = input + std::strlen(input);
    }
    {
        lex_state ls{L, input, iend, PARSE_MODE_NOTCDEF, paridx};
        ast::c_type tp{};
        if (!ls.get() || !parse_type(ls, tp) || !check(ls, -1)) {
            if (ls.err_token() > 0) {
                char buf[16];
                lua_pushfstring(
                    L, "%s near '%s'", ls.get_buf().data(),
                    token_to_str(ls.err_token(), buf)
                );
            } else {
                lua_pushfstring(L, "%s", ls.get_buf().data());
            }
            goto lerr;
        }
        ls.commit();
        return tp;
    }
lerr:
    parse_err(L);
    /* unreachable */
    return ast::c_type{};
}

ast::c_expr_type parse_number(
    lua_State *L, ast::c_value &v, char const *input, char const *iend
) {
    if (!iend) {
        iend = input + std::strlen(input);
    }
    {
        lex_state ls{L, input, iend, PARSE_MODE_NOTCDEF};
        if (!ls.get() || !check(ls, TOK_INTEGER)) {
            if (ls.err_token() > 0) {
                char buf[16];
                lua_pushfstring(
                    L, "%s near '%s'", ls.get_buf().data(),
                    token_to_str(ls.err_token(), buf)
                );
            } else {
                lua_pushfstring(L, "%s", ls.get_buf().data());
            }
            goto lerr;
        }
        v = ls.t.value;
        ls.commit();
        return ls.t.numtag;
    }
lerr:
    parse_err(L);
    /* unreachable */
    return ast::c_expr_type{};
}

void init(lua_State *L) {
    /* init parser state for each lua state */
    auto *p = static_cast<parser_state *>(
        lua_newuserdata(L, sizeof(parser_state))
    );
    new (p) parser_state{};
    /* make sure its destructor is invoked later */
    lua_newtable(L); /* parser_state metatable */
    lua_pushcfunction(L, [](lua_State *LL) -> int {
        auto *pp = lua::touserdata<parser_state>(LL, 1);
        pp->~parser_state();
        return 0;
    });
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);
    /* store */
    lua_setfield(L, LUA_REGISTRYINDEX, lua::CFFI_PARSER_STATE);
    /* initialize keywords */
    init_kwmap(p->keyword_map);
}

} /* namespace parser */
