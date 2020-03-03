#include <cstring>
#include <cctype>
#include <cassert>

#include <string>
#include <unordered_map>
#include <utility>
#include <stdexcept>
#include <memory>

#include "parser.hh"
#include "ast.hh"

namespace parser {

/* define all keywords our subset of C understands */

/* stdint types might as well also be builtin... */
#define KEYWORDS KW(alignof), KW(const), KW(enum), KW(extern), KW(sizeof), \
    KW(struct), KW(typedef), KW(signed), KW(unsigned), KW(volatile), \
    KW(void), \
    \
    KW(__alignof__), KW(__const__), KW(__volatile__), \
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
    KW(_Bool)

/* primary keyword enum */

#define KW(x) TOK_##x

/* a token is an int, single-char tokens are just their ascii */
/* TOK_NAME must be the first pre-keyword token! */
enum c_token {
    TOK_CUSTOM = 257,

    TOK_EQ = TOK_CUSTOM, TOK_NEQ, TOK_GE, TOK_LE,
    TOK_AND, TOK_OR, TOK_LSH, TOK_RSH,

    TOK_INTEGER, TOK_NAME, KEYWORDS
};

#undef KW

/* end primary keyword enum */

/* token strings */

#define KW(x) #x

static char const *tokens[] = {
    "==", "!=", ">=", "<=",
    "&&", "||", "<<", ">>",

    "<integer>", "<name>", KEYWORDS
};

#undef KW

/* end token strings */

/* lexer */

struct lex_token {
    int token = -1;
    ast::c_expr_type numtag = ast::c_expr_type::INVALID;
    std::string value_s;
    lex_token_u value{};
};

static thread_local std::unordered_map<std::string, int> keyword_map;

static void init_kwmap() {
    if (!keyword_map.empty()) {
        return;
    }
    auto nkw = int(
        sizeof(tokens) / sizeof(tokens[0]) + TOK_CUSTOM - TOK_NAME - 1
    );
    for (int i = 1; i <= nkw; ++i) {
        keyword_map[tokens[TOK_NAME - TOK_CUSTOM + i]] = i;
    }
}

struct lex_state_error: public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct lex_state {
    lex_state() = delete;

    lex_state(lua_State *L, char const *str, const char *estr):
        stream(str), send(estr), p_buf{},
        p_dstore{ast::decl_store::get_main(L)}
    {
        /* thread-local, initialize for parsing thread */
        init_kwmap();

        /* this should be enough that we should never have to resize it */
        p_buf.reserve(256);

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

    int get() {
        if (lahead.token >= 0) {
            t = std::move(lahead);
            lahead.token = -1;
            return t.token;
        }
        return (t.token = lex(t));
    }

    int lookahead() {
        return (lahead.token = lex(t));
    }

    void lex_error(std::string const &msg, int) const {
        throw lex_state_error{msg};
    }

    void syntax_error(std::string const &msg) const {
        lex_error(msg, t.token);
    }

    void store_decl(ast::c_object *obj) {
        p_dstore.add(obj);
    }

    void commit() {
        p_dstore.commit();
    }

    ast::c_object const *lookup(std::string const &name) const {
        return p_dstore.lookup(name);
    }

    std::string request_name() const {
        return p_dstore.request_name();
    }

private:
    bool is_newline(int c) {
        return (c == '\n') || (c == '\r');
    }

    char next_char() {
        char ret = current;
        if (stream == send) {
            current = '\0';
            return ret;
        }
        current = *(stream++);
        return ret;
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
        return (val <= U(std::numeric_limits<T>::max()));
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
        lex_error("value out of bounds", TOK_INTEGER);
        return ast::c_expr_type::INVALID;
    }

    template<size_t base, typename F, typename G>
    void read_int_core(F &&digf, G &&convf, lex_token &tok) {
        p_buf.clear();
        do {
            p_buf.push_back(next_char());
        } while (digf(current));
        char const *numbeg = &p_buf[0], *numend = &p_buf[p_buf.size()];
        /* go from the end */
        unsigned long long val = 0, mul = 1;
        do {
            /* standardize case */
            unsigned char dig = convf(*--numend);
            val += dig * mul;
            mul *= base;
        } while (numend != numbeg);
        /* write type and value */
        tok.numtag = get_int_type(tok, val, base == 10);
    }

    void read_integer(lex_token &tok) {
        if (current == '0') {
            next_char();
            if (!current || (
                ((current | 32) != 'x') && ((current | 32) != 'b') &&
                !(current >= '0' && current <= '7')
            )) {
                /* special case: value 0 */
                tok.value.i = 0;
                tok.numtag = ast::c_expr_type::INT;
                return;
            }
            if ((current | 32) == 'x') {
                /* hex */
                next_char();
                if (!isxdigit(current)) {
                    lex_error("malformed integer", TOK_INTEGER);
                    return;
                }
                read_int_core<16>(isxdigit, [](int dig) {
                    dig |= 32;
                    dig = (dig >= 'a') ? (dig - 'a' + 10) : (dig - '0');
                    return dig;
                }, tok);
            } else if ((current | 32) == 'b') {
                /* binary */
                next_char();
                if ((current != '0') && (current != '1')) {
                    lex_error("malformed integer", TOK_INTEGER);
                    return;
                }
                read_int_core<2>([](int cur) {
                    return (cur == '0') || (cur == '1');
                }, [](int dig) {
                    return (dig - '0');
                }, tok);
            } else {
                /* octal */
                read_int_core<8>([](int cur) {
                    return (cur >= '0') && (cur <= '7');
                }, [](int dig) {
                    return (dig - '0');
                }, tok);
            }
        } else {
            /* decimal */
            read_int_core<10>(isdigit, [](int dig) {
                return (dig - '0');
            }, tok);
        }
    }

    int lex(lex_token &tok) {
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
                    syntax_error("unterminated comment");
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
            /* single-char tokens, number literals, keywords, names */
            default: {
                if (isspace(current)) {
                    next_char();
                    continue;
                } else if (isdigit(current)) {
                    read_integer(tok);
                    return TOK_INTEGER;
                }
                if (isalpha(current) || (current == '_')) {
                    /* names, keywords */
                    /* what current pointed to */
                    /* keep reading until we readh non-matching char */
                    p_buf.clear();
                    do {
                        p_buf.push_back(next_char());
                    } while (isalnum(current) || (current == '_'));
                    std::string name{p_buf.begin(), p_buf.end()};
                    /* could be a keyword? */
                    auto kwit = keyword_map.find(name);
                    tok.value_s = std::move(name);
                    if (kwit != keyword_map.end()) {
                        return TOK_NAME + kwit->second;
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

    char const *stream;
    char const *send;

    std::vector<char> p_buf;
    ast::decl_store p_dstore;

public:
    int line_number = 1;
    lex_token t, lahead;
};

static std::string token_to_str(int tok) {
    std::string ret;
    if (tok < 0) {
        return "<eof>";
    }
    if (tok < TOK_CUSTOM) {
        if (isprint(tok)) {
            ret += char(tok);
        } else {
            char buf[16];
            snprintf(buf, sizeof(buf), "char(%d)", tok);
            ret += buf;
        }
        return ret;
    }
    return tokens[tok - TOK_CUSTOM];
}

/* parser */

static void
error_expected(lex_state &ls, int tok) {
    std::string buf;
    buf += '\'';
    buf += token_to_str(tok);
    buf += "' expected";
    ls.syntax_error(buf);
}

static bool test_next(lex_state &ls, int tok) {
    if (ls.t.token == tok) {
        ls.get();
        return true;
    }
    return false;
}

static void check(lex_state &ls, int tok) {
    if (ls.t.token != tok) {
        error_expected(ls, tok);
    }
}

static void check_next(lex_state &ls, int tok) {
    check(ls, tok);
    ls.get();
}

static void check_match(lex_state &ls, int what, int who, int where) {
    if (test_next(ls, what)) {
        return;
    }
    if (where == ls.line_number) {
        error_expected(ls, what);
    } else {
        char lbuf[16];
        auto tbuf = token_to_str(what);
        auto vbuf = token_to_str(who);
        std::string buf;
        buf += '\'';
        buf += tbuf;
        buf += " ' expected (to close '";
        buf += vbuf;
        buf += "' at line ";
        snprintf(lbuf, sizeof(lbuf), "%d", where);
        buf += lbuf;
        buf += ')';
        ls.syntax_error(buf);
    }
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

static std::unique_ptr<ast::c_expr> expr_dup(ast::c_expr &&exp) {
    return std::make_unique<ast::c_expr>(std::move(exp));
}

static ast::c_expr parse_cexpr(lex_state &ls);
static ast::c_expr parse_cexpr_bin(lex_state &ls, int min_prec);

static ast::c_type parse_type(
    lex_state &ls, bool allow_void = false, std::string *fpname = nullptr
);

static ast::c_expr parse_cexpr_simple(lex_state &ls) {
    auto unop = get_unop(ls.t.token);
    if (unop != ast::c_expr_unop::INVALID) {
        ls.get();
        auto exp = parse_cexpr_bin(ls, unprec);
        ast::c_expr unexp;
        unexp.type = ast::c_expr_type::UNARY;
        unexp.un.op = unop;
        unexp.un.expr = expr_dup(std::move(exp)).release();
        return unexp;
    }
    /* FIXME: implement non-integer constants */
    switch (ls.t.token) {
        case TOK_INTEGER: {
            ast::c_expr ret;
            ret.type = ls.t.numtag;
            memcpy(&ret.val, &ls.t.value, sizeof(ls.t.value));
            ls.get();
            return ret;
        }
        case TOK_sizeof: {
            /* TODO: this should also take expressions
             * we just don't support expressions this would support yet
             */
            ls.get();
            int line = ls.line_number;
            check_next(ls, '(');
            auto tp = parse_type(ls);
            check_match(ls, ')', '(', line);
            ast::c_expr ret;
            size_t align = tp.libffi_type()->size;
            if (sizeof(unsigned long long) > sizeof(void *)) {
                ret.type = ast::c_expr_type::ULONG;
                ret.val.ul = static_cast<unsigned long>(align);
            } else {
                ret.type = ast::c_expr_type::ULLONG;
                ret.val.ull = static_cast<unsigned long long>(align);
            }
            return ret;
        }
        case TOK_alignof:
        case TOK___alignof__: {
            ls.get();
            int line = ls.line_number;
            check_next(ls, '(');
            auto tp = parse_type(ls);
            check_match(ls, ')', '(', line);
            ast::c_expr ret;
            size_t align = tp.libffi_type()->alignment;
            if (sizeof(unsigned long long) > sizeof(void *)) {
                ret.type = ast::c_expr_type::ULONG;
                ret.val.ul = static_cast<unsigned long>(align);
            } else {
                ret.type = ast::c_expr_type::ULLONG;
                ret.val.ull = static_cast<unsigned long long>(align);
            }
            return ret;
        }
        case '(': {
            int line = ls.line_number;
            ls.get();
            auto ret = parse_cexpr(ls);
            check_match(ls, ')', '(', line);
            return ret;
        }
        default:
            ls.syntax_error("unexpected symbol");
            break;
    }
    return ast::c_expr{}; /* unreachable */
}

static ast::c_expr parse_cexpr_bin(lex_state &ls, int min_prec) {
    auto lhs = parse_cexpr_simple(ls);
    for (;;) {
        bool istern = (ls.t.token == '?');
        ast::c_expr_binop op;
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
        ls.get();
        if (istern) {
            ast::c_expr texp = parse_cexpr(ls);
            check_next(ls, ':');
            ast::c_expr fexp = parse_cexpr_bin(ls, ifprec);
            ast::c_expr tern;
            tern.type = ast::c_expr_type::TERNARY;
            tern.tern.cond = expr_dup(std::move(lhs)).release();
            tern.tern.texpr = expr_dup(std::move(texp)).release();
            tern.tern.fexpr = expr_dup(std::move(fexp)).release();
            lhs = std::move(tern);
            continue;
        }
        /* for right associative this would be prec, we don't
         * have those except ternary which is handled specially
         */
        int nprec = prec + 1;
        ast::c_expr rhs = parse_cexpr_bin(ls, nprec);
        ast::c_expr bin;
        bin.type = ast::c_expr_type::BINARY;
        bin.bin.op = op;
        bin.bin.lhs = expr_dup(std::move(lhs)).release();
        bin.bin.rhs = expr_dup(std::move(rhs)).release();
        lhs = std::move(bin);
    }
    return lhs;
}

static ast::c_expr parse_cexpr(lex_state &ls) {
    return parse_cexpr_bin(ls, 1);
}

static int parse_cv(lex_state &ls) {
    int quals = 0;

    for (;;) switch (ls.t.token) {
        case TOK_const:
        case TOK___const__:
            if (quals & ast::C_CV_CONST) {
                ls.syntax_error("duplicate const qualifier");
                break;
            }
            ls.get();
            quals |= ast::C_CV_CONST;
            break;
        case TOK_volatile:
        case TOK___volatile__:
            if (quals & ast::C_CV_VOLATILE) {
                ls.syntax_error("duplicate volatile qualifier");
                break;
            }
            ls.get();
            quals |= ast::C_CV_VOLATILE;
            break;
        default:
            return quals;
    }

    return quals;
}

static std::vector<ast::c_param> parse_paramlist(lex_state &ls) {
    int linenum = ls.line_number;
    ls.get();

    std::vector<ast::c_param> params;

    if (ls.t.token == ')') {
        goto done_params;
    }

    for (;;) {
        auto pt = parse_type(ls, params.size() == 0);
        if (pt.type() == ast::C_BUILTIN_VOID) {
            break;
        }
        if (test_next(ls, ',')) {
            /* unnamed param */
            continue;
        }
        check(ls, TOK_NAME);
        params.emplace_back(ls.t.value_s, std::move(pt));
        ls.get();
        if (!test_next(ls, ',')) {
            break;
        }
    }

done_params:
    check_match(ls, ')', '(', linenum);

    return params;
}

static ast::c_type parse_type_ptr(
    lex_state &ls, ast::c_type tp, bool allow_void, std::string *fpname
) {
    for (;;) {
        /* right-side cv */
        tp.cv(parse_cv(ls));

        if (ls.t.token == '(') {
            /* function pointer */
            ls.get();
            check_next(ls, '*');
            int pcv = parse_cv(ls);
            if (fpname) {
                check(ls, TOK_NAME);
                *fpname = ls.t.value_s;
                ls.get();
            }
            check_next(ls, ')');
            auto *cf = new ast::c_function{
                ls.request_name(), std::move(tp), parse_paramlist(ls)
            };
            ls.store_decl(cf);
            ast::c_type ftp{cf, pcv};
            return ftp;
        }

        /* for contexts where void is not allowed,
         * anything void-based must be a pointer
         */
        if (!allow_void && (tp.type() == ast::C_BUILTIN_VOID)) {
            check(ls, '*');
        }

        if (ls.t.token == '&') {
            /* C++ reference */
            ls.get();
            return ast::c_type{std::move(tp), 0, ast::C_BUILTIN_REF};
        }

        /* pointers plus their right side qualifiers */
        if (ls.t.token == '*') {
            ls.get();
            ast::c_type ptp{std::move(tp), parse_cv(ls)};
            using CT = ast::c_type;
            tp.~CT();
            new (&tp) ast::c_type{std::move(ptp)};
        } else {
            break;
        }
    }

    return tp;
}

enum type_signedness {
    TYPE_SIGNED = 1 << 0,
    TYPE_UNSIGNED = 1 << 1
};

/* a bit naive for now, and builtins could be handled better (we don't care
 * about the real signatures, only about their sizes and signedness and so
 * on to provide to the codegen) but it's a start
 */
static ast::c_type parse_type(
    lex_state &ls, bool allow_void, std::string *fpn
) {
    /* left-side cv */
    int quals = parse_cv(ls);
    int squals = 0;

    std::string tname{};
    ast::c_builtin cbt = ast::C_BUILTIN_INVALID;

    if (ls.t.token == TOK_signed || ls.t.token == TOK_unsigned) {
        if (ls.t.token == TOK_signed) {
            squals |= TYPE_SIGNED;
        } else {
            squals |= TYPE_UNSIGNED;
        }
        ls.get();
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
            tname = "int";
        } else {
            cbt = ast::C_BUILTIN_UINT;
            tname = "unsigned int";
        }
        goto newtype;
    } else if (ls.t.token == TOK_struct || ls.t.token == TOK_enum) {
        tname = ls.t.value_s;
        ls.get();
        tname += ' ';
        check(ls, TOK_NAME);
    }

qualified:
    if (ls.t.token == TOK_NAME) {
        /* typedef, struct, enum, var, etc. */
        tname += ls.t.value_s;
        auto *decl = ls.lookup(tname);
        if (!decl) {
            std::string buf;
            buf += "undeclared symbol '";
            buf += tname;
            buf += "'";
            ls.syntax_error(buf);
        }
        ls.get();
        switch (decl->obj_type()) {
            case ast::c_object_type::TYPEDEF: {
                ast::c_type tp{decl->as<ast::c_typedef>().type()};
                /* merge qualifiers */
                tp.cv(quals);
                return parse_type_ptr(ls, std::move(tp), allow_void, fpn);
            }
            case ast::c_object_type::STRUCT: {
                auto &tp = decl->as<ast::c_struct>();
                return parse_type_ptr(ls, ast::c_type{&tp, quals}, true, fpn);
            }
            case ast::c_object_type::ENUM: {
                auto &tp = decl->as<ast::c_enum>();
                return parse_type_ptr(ls, ast::c_type{&tp, quals}, true, fpn);
            }
            default: {
                std::string buf;
                buf += "symbol '";
                buf += tname;
                buf += "' is not a type";
                ls.syntax_error(buf);
                break;
            }
        }
    } else switch (ls.t.token) {
        /* may be a builtin type */
        case TOK_void:
            cbt = ast::C_BUILTIN_VOID;
            goto btype;
        case TOK_int8_t:
            cbt = ast::C_BUILTIN_INT8;
            goto btype;
        case TOK_int16_t:
            cbt = ast::C_BUILTIN_INT16;
            goto btype;
        case TOK_int32_t:
            cbt = ast::C_BUILTIN_INT32;
            goto btype;
        case TOK_int64_t:
            cbt = ast::C_BUILTIN_INT64;
            goto btype;
        case TOK_uint8_t:
            cbt = ast::C_BUILTIN_UINT8;
            goto btype;
        case TOK_uint16_t:
            cbt = ast::C_BUILTIN_UINT16;
            goto btype;
        case TOK_uint32_t:
            cbt = ast::C_BUILTIN_UINT32;
            goto btype;
        case TOK_uint64_t:
            cbt = ast::C_BUILTIN_UINT64;
            goto btype;
        case TOK_uintptr_t:
            cbt = ast::C_BUILTIN_UINTPTR;
            goto btype;
        case TOK_intptr_t:
            cbt = ast::C_BUILTIN_INTPTR;
            goto btype;
        case TOK_ptrdiff_t:
            cbt = ast::C_BUILTIN_PTRDIFF;
            goto btype;
        case TOK_ssize_t:
            cbt = ast::C_BUILTIN_SSIZE;
            goto btype;
        case TOK_size_t:
            cbt = ast::C_BUILTIN_SIZE;
            goto btype;
        case TOK_time_t:   cbt = ast::C_BUILTIN_TIME;   goto btype;
        case TOK_wchar_t:  cbt = ast::C_BUILTIN_WCHAR;  goto btype;
        case TOK_char16_t: cbt = ast::C_BUILTIN_CHAR16; goto btype;
        case TOK_char32_t: cbt = ast::C_BUILTIN_CHAR32; goto btype;
        case TOK_float:    cbt = ast::C_BUILTIN_FLOAT;  goto btype;
        case TOK_double:   cbt = ast::C_BUILTIN_DOUBLE; goto btype;
        case TOK_bool:
        case TOK__Bool:
            cbt = ast::C_BUILTIN_BOOL;
        btype:
            tname = ls.t.value_s;
            ls.get();
            break;
        case TOK_char:
            if (squals & TYPE_SIGNED) {
                cbt = ast::C_BUILTIN_SCHAR;
                tname = "signed char";
            } else if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_UCHAR;
                tname = "unsigned char";
            } else {
                cbt = ast::C_BUILTIN_CHAR;
                tname = "char";
            }
            ls.get();
            break;
        case TOK_short:
            if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_USHORT;
                tname = "unsigned short";
            } else {
                cbt = ast::C_BUILTIN_SHORT;
                tname = "short";
            }
            ls.get();
            break;
        case TOK_int:
            if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_UINT;
                tname = "unsigned int";
            } else {
                cbt = ast::C_BUILTIN_INT;
                tname = "int";
            }
            ls.get();
            break;
        case TOK_long:
            ls.get();
            if (ls.t.token == TOK_long) {
                if (squals & TYPE_UNSIGNED) {
                    cbt = ast::C_BUILTIN_ULLONG;
                    tname = "unsigned long long";
                } else {
                    cbt = ast::C_BUILTIN_LLONG;
                    tname = "long long";
                }
                ls.get();
            } else if (ls.t.token == TOK_int) {
                if (squals & TYPE_UNSIGNED) {
                    cbt = ast::C_BUILTIN_ULONG;
                    tname = "unsigned long";
                } else {
                    cbt = ast::C_BUILTIN_LONG;
                    tname = "long";
                }
                ls.get();
            } else if (ls.t.token == TOK_double) {
                cbt = ast::C_BUILTIN_LDOUBLE;
                tname = "long double";
                ls.get();
            } else if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_ULONG;
                tname = "unsigned long";
            } else {
                cbt = ast::C_BUILTIN_LONG;
                tname = "long";
            }
            break;
        default:
            ls.syntax_error("type name expected");
            break;
    }

newtype:
    assert(cbt != ast::C_BUILTIN_INVALID);
    return parse_type_ptr(ls, ast::c_type{tname, cbt, quals}, allow_void, fpn);
}

/* two syntaxes allowed by C:
 * typedef FROM TO;
 * FROM typedef TO;
 */
static void parse_typedef(lex_state &ls, ast::c_type &&tp, std::string &aname) {
    /* the new type name */
    if (aname.empty()) {
        check(ls, TOK_NAME);
        aname = ls.t.value_s;
        ls.get();
    }

    ls.store_decl(new ast::c_typedef{std::move(aname), std::move(tp)});
}

static void parse_struct(lex_state &ls) {
    ls.get(); /* struct keyword */

    /* name is optional */
    std::string sname = "struct ";
    if (ls.t.token == TOK_NAME) {
        sname += ls.t.value_s;
        ls.get();
    } else {
        sname += ls.request_name();
    }

    int linenum = ls.line_number;
    check_next(ls, '{');

    std::vector<ast::c_struct::field> fields;

    while (ls.t.token != '}') {
        std::string fname;
        auto ft = parse_type(ls, false, &fname);
        if (fname.empty()) {
            check(ls, TOK_NAME);
            fname = ls.t.value_s;
            ls.get();
        }
        fields.emplace_back(std::move(fname), std::move(ft));
        check_next(ls, ';');
    }

    check_match(ls, '}', '{', linenum);

    ls.store_decl(new ast::c_struct{std::move(sname), std::move(fields)});
}

static void parse_enum(lex_state &ls) {
    ls.get();

    /* name is optional */
    std::string ename = "enum ";
    if (ls.t.token == TOK_NAME) {
        ename += ls.t.value_s;
        ls.get();
    } else {
        ename += ls.request_name();
    }

    int linenum = ls.line_number;
    check_next(ls, '{');

    std::vector<ast::c_enum::field> fields;

    while (ls.t.token != '}') {
        check(ls, TOK_NAME);
        std::string fname = ls.t.value_s;
        ls.get();
        if (ls.t.token == '=') {
            ls.get();
            auto exp = parse_cexpr(ls);
            auto val = exp.eval();
            fields.emplace_back(std::move(fname), val.i);
        } else {
            fields.emplace_back(
                std::move(fname), fields.empty() ? 0 : (fields.back().value + 1)
            );
        }
        if (ls.t.token != ',') {
            break;
        } else {
            ls.get();
        }
    }

    check_match(ls, '}', '{', linenum);

    ls.store_decl(new ast::c_enum{std::move(ename), std::move(fields)});
}

static void parse_decl(lex_state &ls) {
    std::string dname;
    switch (ls.t.token) {
        case TOK_typedef: {
            /* syntax 1: typedef FROM TO; */
            ls.get();
            parse_typedef(ls, parse_type(ls, true, &dname), dname);
            return;
        }
        case TOK_struct:
            parse_struct(ls);
            return;
        case TOK_enum:
            parse_enum(ls);
            return;
        case TOK_extern:
            /* may precede any declaration without changing its behavior */
            ls.get();
            break;
    }

    auto tp = parse_type(ls, true, &dname);
    if (ls.t.token == TOK_typedef) {
        /* weird ass infix syntax: FROM typedef TO; */
        ls.get();
        parse_typedef(ls, std::move(tp), dname);
        return;
    }

    bool fptr = false;
    if (dname.empty()) {
        check(ls, TOK_NAME);
        dname = ls.t.value_s;
        ls.get();
    } else {
        fptr = true;
    }

    /* leftmost type is plain void; so it must be a function */
    if (tp.type() == ast::C_BUILTIN_VOID) {
        check(ls, '(');
    }

    if (ls.t.token == ';') {
        ls.store_decl(new ast::c_variable{std::move(dname), std::move(tp)});
        return;
    } else if (ls.t.token != '(') {
        check(ls, ';');
        return;
    }

    if (fptr) {
        /* T (*name)(params)(params); */
        ls.syntax_error("function declaration returning a function");
    }

    ls.store_decl(new ast::c_function{
        std::move(dname), std::move(tp), parse_paramlist(ls)
    });
}

static void parse_decls(lex_state &ls) {
    while (ls.t.token >= 0) {
        if (ls.t.token == ';') {
            /* empty statement */
            ls.get();
            continue;
        }
        parse_decl(ls);
        if (!ls.t.token) {
            break;
        }
        check_next(ls, ';');
    }
}

void parse(lua_State *L, char const *input, char const *iend) {
    if (!iend) {
        iend = input + strlen(input);
    }
    lex_state ls{L, input, iend};

    /* read first token */
    ls.get();

    parse_decls(ls);
    ls.commit();
}

ast::c_type parse_type(lua_State *L, char const *input, char const *iend) {
    if (!iend) {
        iend = input + strlen(input);
    }
    lex_state ls{L, input, iend};
    ls.get();
    auto tp = parse_type(ls);
    ls.commit();
    return tp;
}

ast::c_expr_type parse_number(
    lua_State *L, lex_token_u &v, char const *input, char const *iend
) {
    if (!iend) {
        iend = input + strlen(input);
    }
    lex_state ls{L, input, iend};
    ls.get();
    check(ls, TOK_INTEGER);
    v = ls.t.value;
    ls.commit();
    return ls.t.numtag;
}

} /* namespace parser */
