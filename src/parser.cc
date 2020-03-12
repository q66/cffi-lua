#include <cstring>
#include <cctype>
#include <cassert>

#include <stack>
#include <deque>
#include <string>
#include <unordered_map>
#include <utility>
#include <stdexcept>
#include <memory>
#include <limits>

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

    TOK_ELLIPSIS,

    TOK_INTEGER, TOK_NAME, KEYWORDS
};

#undef KW

/* end primary keyword enum */

/* token strings */

#define KW(x) #x

static char const *tokens[] = {
    "==", "!=", ">=", "<=",
    "&&", "||", "<<", ">>",

    "...",

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

static thread_local std::unordered_map<
    char const *, int, util::str_hash, util::str_equal
> keyword_map;

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
    lex_state_error(std::string const &str, int tok, int lnum):
        std::runtime_error{str}, token{tok}, line_number{lnum}
    {}

    int token;
    int line_number;
};

enum parse_mode {
    PARSE_MODE_DEFAULT,
    PARSE_MODE_NOTCDEF
};

struct lex_state {
    lex_state() = delete;

    lex_state(
        lua_State *L, char const *str, const char *estr,
        int pmode = PARSE_MODE_DEFAULT
    ):
        p_mode(pmode), stream(str), send(estr), p_buf{},
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

    void lex_error(std::string const &msg, int tok) const {
        throw lex_state_error{msg, tok, line_number};
    }

    void syntax_error(std::string const &msg) const {
        lex_error(msg, t.token);
    }

    void store_decl(ast::c_object *obj, int lnum) {
        try {
            p_dstore.add(obj);
        } catch (ast::redefine_error const &e) {
            throw lex_state_error{e.what(), -1, lnum};
        }
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

    std::string request_name() const {
        return p_dstore.request_name();
    }

    int mode() const {
        return p_mode;
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
                    auto kwit = keyword_map.find(name.c_str());
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
    int p_mode = PARSE_MODE_DEFAULT;

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
            ret += static_cast<char const *>(buf);
        }
        return ret;
    }
    return tokens[tok - TOK_CUSTOM];
}

/* parser */

static void error_expected(lex_state &ls, int tok) {
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
        buf += static_cast<char const *>(lbuf);
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
    lex_state &ls, bool allow_void = false, std::string *fpname = nullptr,
    bool needn = true
);

static ast::c_struct const &parse_struct(lex_state &ls, bool *newst = nullptr);
static ast::c_enum const &parse_enum(lex_state &ls);

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

static size_t get_arrsize(lex_state &ls, ast::c_expr const &exp) {
    ast::c_expr_type et;
    auto val = exp.eval(et, true);

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
            ls.syntax_error("invalid array size");
            break;
    }
    if (sval < 0) {
        ls.syntax_error("array size is negative");
    }
    uval = sval;

done:
    using ULL = unsigned long long;
    if (uval > ULL(std::numeric_limits<size_t>::max())) {
        ls.syntax_error("array size too big");
    }
    return size_t(uval);
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

struct arrdim {
    size_t size;
    int quals;
};

/* FIXME: when in var declarations, all components must be complete */
static bool parse_array(lex_state &ls, bool &vla, std::stack<arrdim> &dims) {
    if (ls.t.token != '[') {
        return false;
    }
    ls.get();
    if (ls.t.token == ']') {
        vla = true;
        int cv = parse_cv(ls);
        dims.push({0, cv});
        ls.get();
    } else {
        vla = false;
        int cv = parse_cv(ls);
        dims.push({get_arrsize(ls, parse_cexpr(ls)), cv});
        check_next(ls, ']');
    }
    while (ls.t.token == '[') {
        ls.get();
        int cv = parse_cv(ls);
        dims.push({get_arrsize(ls, parse_cexpr(ls)), cv});
        check_next(ls, ']');
    }
    return true;
}

static std::vector<ast::c_param> parse_paramlist(lex_state &ls) {
    int linenum = ls.line_number;
    ls.get();

    std::vector<ast::c_param> params;

    if (ls.t.token == ')') {
        goto done_params;
    }

    for (;;) {
        if (ls.t.token == TOK_ELLIPSIS) {
            /* varargs, insert a sentinel type (will be dropped) */
            params.emplace_back(std::string{}, ast::c_type{
                ast::C_BUILTIN_VOID, 0
            });
            ls.get();
            /* varargs ends the arglist */
            break;
        }
        std::string pname;
        auto pt = parse_type(ls, params.size() == 0, &pname, false);
        if (pt.type() == ast::C_BUILTIN_VOID) {
            break;
        }
        if (pname == "?") {
            pname.clear();
        }
        params.emplace_back(std::move(pname), std::move(pt));
        if (!test_next(ls, ',')) {
            break;
        }
    }

done_params:
    check_match(ls, ')', '(', linenum);

    return params;
}

/* represents a level of parens pair when parsing a type; so e.g. in
 *
 * void (*(*(*(*&))))
 *
 * we have 4 levels.
 */
struct plevel {
    plevel():
        cv{0}, is_term{false}, is_func{false}, is_ref{false},
        is_arr{false}, is_vla{false}
    {}

    std::vector<ast::c_param> argl{};
    std::stack<arrdim> arrd{};
    int cv: 16;
    int is_term: 1;
    int is_func: 1;
    int is_ref: 1;
    int is_arr: 1;
    int is_vla: 1;
};

/* FIXME: optimize, right now this uses more memory than necessary
 *
 * this attempts to implement the complete syntax of how types are parsed
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
static ast::c_type parse_type_ptr(
    lex_state &ls, ast::c_type tp, bool allow_void,
    std::string *fpname, bool needn
) {
    /* our input is the left-side qualified type; that means constructs such
     * as 'const int' or 'const unsigned long int'; that much is parsed by
     * the function that wraps this one
     */

    /* that means we start with parsing the right-side cv qualfiers that may
     * follow; at this point we've parsed e.g. 'char const'
     */
    tp.cv(parse_cv(ls));

    /*
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
     * first we define a double ended queue containing 'levels'... each level
     * denotes one matched pair of parens, except the implicit default level
     * which is always added; new level is delimited by a sentinel value,
     * and the elements past the sentinel can specify pointers and references
     *
     */
    std::deque<plevel> pcvq;
    /* normally we'd consume the '(', but remember, first level is implicit */
    goto newlevel;
    do {
        ls.get();
newlevel:
        /* create the sentinel */
        pcvq.emplace_back();
        pcvq.back().is_term = true;
        /* count all '*' and create element for each */
        while (ls.t.token == '*') {
            ls.get();
            pcvq.emplace_back();
            pcvq.back().cv = parse_cv(ls);
        }
        /* references are handled the same, but we know there can only be
         * one of them; this actually does not cover all cases, since putting
         * parenthesis after this will allow you to specify another reference,
         * but filter this trivial case early on since we can */
        if (ls.t.token == '&') {
            ls.get();
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
            switch (ls.lookahead()) {
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
    /* if 'fpname' was passed, it means we might want to handle a named type
     * or declaration, with the name being optional or mandatory depending
     * on 'needn' - if name was optionally requested but not found, we write
     * a dummy value to tell the caller what happened
     */
    if (fpname) {
        if (needn || (ls.t.token == TOK_NAME)) {
            /* we're in a context where name can be provided, e.g. if
             * parsing a typedef or a prototype, this will be the name
             */
            check(ls, TOK_NAME);
            *fpname = ls.t.value_s;
            ls.get();
        } else {
            *fpname = "?";
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
    for (auto it = pcvq.rbegin();;) {
        plevel &clev = *it;
        if (!clev.is_term) { /* skip non-sentinels */
            ++it;
            continue;
        }
        if (ls.t.token == '(') {
            /* we know it's a paramlist, since all starting '(' of levels
             * are already consumed since before
             */
            clev.argl = parse_paramlist(ls);
            clev.is_func = true;
        } else if (ls.t.token == '[') {
            /* array dimensions may be multiple */
            bool vla;
            clev.is_arr = parse_array(ls, vla, clev.arrd);
            clev.is_vla = vla;
        }
        ++it;
        /* special case of the implicit level, it's not present in syntax */
        if (it == pcvq.rend()) {
            break;
        }
        check_next(ls, ')');
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
    plevel *olev = &pcvq.front();
    for (auto it = pcvq.begin() + 1;;) {
        using CT = ast::c_type;
        if (olev->is_func) {
            /* outer level has an arglist */
            bool variadic = false;
            if (!olev->argl.empty() && (
                olev->argl.back().type().type() == ast::C_BUILTIN_VOID
            )) {
                variadic = true;
                olev->argl.pop_back();
            }
            auto *cf = new ast::c_function{
                ls.request_name(), std::move(tp),
                std::move(olev->argl), variadic
            };
            ls.store_decl(cf, 0);
            tp.~CT();
            new (&tp) ast::c_type{cf, 0};
        } else if (olev->is_arr) {
            while (!olev->arrd.empty()) {
                size_t dim = olev->arrd.top().size;
                int quals = olev->arrd.top().quals;
                olev->arrd.pop();
                ast::c_type atp{
                    std::move(tp), quals, dim,
                    olev->is_vla && olev->arrd.empty()
                };
                tp.~CT();
                new (&tp) ast::c_type{std::move(atp)};
            }
        }
        /* we only had the implicit level all along, so break out */
        if (it == pcvq.end()) {
            break;
        }
        /* only set the new outer if it's a new sentinel */
        if (it->is_term) {
            olev = &*it;
            ++it;
        }
        /* bind pointers and references to whatever is 'tp', which will
         * be a new thing if an arglist/array is present, and the previous
         * type if not
         */
        while ((it != pcvq.end()) && !it->is_term) {
            /* references are trailing, we can't make pointers
             * to them nor we can make references to references
             */
            if (tp.type() == ast::C_BUILTIN_REF) {
                ls.syntax_error("references must be trailing");
            }
            ast::c_type ntp{
                std::move(tp), it->cv,
                it->is_ref ? ast::C_BUILTIN_REF : ast::C_BUILTIN_PTR
            };
            tp.~CT();
            new (&tp) ast::c_type{std::move(ntp)};
            ++it;
        }
    }
    /* one last thing: if plain void type is not allowed in this context
     * and we nevertheless got it, we need to error
     */
    if (!allow_void && (tp.type() == ast::C_BUILTIN_VOID)) {
        ls.syntax_error("void type in forbidden context");
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
    lex_state &ls, bool allow_void, std::string *fpn, bool needn
) {
    /* left-side cv */
    int quals = parse_cv(ls);
    int squals = 0;

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
        } else {
            cbt = ast::C_BUILTIN_UINT;
        }
        goto newtype;
    } else if (ls.t.token == TOK_struct) {
        return parse_type_ptr(
            ls, ast::c_type{&parse_struct(ls), quals}, true, fpn, needn
        );
    } else if (ls.t.token == TOK_enum) {
        return parse_type_ptr(
            ls, ast::c_type{&parse_enum(ls), quals}, true, fpn, needn
        );
    }

qualified:
    if (ls.t.token == TOK_NAME) {
        /* typedef, struct, enum, var, etc. */
        auto *decl = ls.lookup(ls.t.value_s.c_str());
        if (!decl) {
            std::string buf;
            buf += "undeclared symbol '";
            buf += ls.t.value_s;
            buf += "'";
            ls.syntax_error(buf);
        }
        switch (decl->obj_type()) {
            case ast::c_object_type::TYPEDEF: {
                ls.get();
                ast::c_type tp{decl->as<ast::c_typedef>().type()};
                /* merge qualifiers */
                tp.cv(quals);
                return parse_type_ptr(
                    ls, std::move(tp), allow_void, fpn, needn
                );
            }
            case ast::c_object_type::STRUCT: {
                ls.get();
                auto &tp = decl->as<ast::c_struct>();
                return parse_type_ptr(
                    ls, ast::c_type{&tp, quals}, true, fpn, needn
                );
            }
            case ast::c_object_type::ENUM: {
                ls.get();
                auto &tp = decl->as<ast::c_enum>();
                return parse_type_ptr(
                    ls, ast::c_type{&tp, quals}, true, fpn, needn
                );
            }
            default: {
                std::string buf;
                buf += "symbol '";
                buf += ls.t.value_s;
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
        case TOK_va_list:
        case TOK___builtin_va_list:
        case TOK___gnuc_va_list:
            cbt = ast::C_BUILTIN_VA_LIST;
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
            ls.get();
            break;
        case TOK_char:
            if (squals & TYPE_SIGNED) {
                cbt = ast::C_BUILTIN_SCHAR;
            } else if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_UCHAR;
            } else {
                cbt = ast::C_BUILTIN_CHAR;
            }
            ls.get();
            break;
        case TOK_short:
            if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_USHORT;
            } else {
                cbt = ast::C_BUILTIN_SHORT;
            }
            ls.get();
            break;
        case TOK_int:
            if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_UINT;
            } else {
                cbt = ast::C_BUILTIN_INT;
            }
            ls.get();
            break;
        case TOK_long:
            ls.get();
            if (ls.t.token == TOK_long) {
                if (squals & TYPE_UNSIGNED) {
                    cbt = ast::C_BUILTIN_ULLONG;
                } else {
                    cbt = ast::C_BUILTIN_LLONG;
                }
                ls.get();
            } else if (ls.t.token == TOK_int) {
                if (squals & TYPE_UNSIGNED) {
                    cbt = ast::C_BUILTIN_ULONG;
                } else {
                    cbt = ast::C_BUILTIN_LONG;
                }
                ls.get();
            } else if (ls.t.token == TOK_double) {
                cbt = ast::C_BUILTIN_LDOUBLE;
                ls.get();
            } else if (squals & TYPE_UNSIGNED) {
                cbt = ast::C_BUILTIN_ULONG;
            } else {
                cbt = ast::C_BUILTIN_LONG;
            }
            break;
        default:
            ls.syntax_error("type name expected");
            break;
    }

newtype:
    assert(cbt != ast::C_BUILTIN_INVALID);
    return parse_type_ptr(ls, ast::c_type{cbt, quals}, allow_void, fpn, needn);
}

/* two syntaxes allowed by C:
 * typedef FROM TO;
 * FROM typedef TO;
 */
static void parse_typedef(
    lex_state &ls, ast::c_type &&tp, std::string &aname, int tline
) {
    /* the new type name */
    if (aname.empty()) {
        check(ls, TOK_NAME);
        aname = ls.t.value_s;
        ls.get();
    }

    ls.store_decl(new ast::c_typedef{std::move(aname), std::move(tp)}, tline);
}

static ast::c_struct const &parse_struct(lex_state &ls, bool *newst) {
    int sline = ls.line_number;
    ls.get(); /* struct keyword */

    /* name is optional */
    bool named = false;
    std::string sname = "struct ";
    if (ls.t.token == TOK_NAME) {
        sname += ls.t.value_s;
        ls.get();
        named = true;
    } else {
        sname += ls.request_name();
    }

    int linenum = ls.line_number;

    auto mode_error = [&ls, named]() {
        if (named && (ls.mode() == PARSE_MODE_NOTCDEF)) {
            ls.syntax_error("struct declaration not allowed in this context");
        }
    };

    /* opaque */
    if (!test_next(ls, '{')) {
        auto *oldecl = ls.lookup(sname.c_str());
        if (!oldecl || (oldecl->obj_type() != ast::c_object_type::STRUCT)) {
            mode_error();
            /* different type or not stored yet, raise error or store */
            auto *p = new ast::c_struct{std::move(sname)};
            ls.store_decl(p, sline);
            return *p;
        }
        return oldecl->as<ast::c_struct>();
    }

    mode_error();

    std::vector<ast::c_struct::field> fields;

    while (ls.t.token != '}') {
        std::string fname{};
        if (ls.t.token == TOK_struct) {
            bool transp = false;
            auto &st = parse_struct(ls, &transp);
            if (transp && test_next(ls, ';')) {
                fields.emplace_back(fname, ast::c_type{&st, 0});
                continue;
            }
            auto tp = parse_type_ptr(
                ls, ast::c_type{&st, 0}, true, &fname, true
            );
            if (fname.empty()) {
                check(ls, TOK_NAME);
                fname = ls.t.value_s;
                ls.get();
            }
            fields.emplace_back(std::move(fname), std::move(tp));
            continue;
        }
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

    auto *oldecl = ls.lookup(sname.c_str());
    if (oldecl && (oldecl->obj_type() == ast::c_object_type::STRUCT)) {
        auto &st = oldecl->as<ast::c_struct>();
        if (st.opaque()) {
            /* previous declaration was opaque; prevent redef errors */
            st.set_fields(std::move(fields));
            if (newst) {
                *newst = true;
            }
            return st;
        }
    }

    if (newst) {
        *newst = true;
    }
    auto *p = new ast::c_struct{std::move(sname), std::move(fields)};
    ls.store_decl(p, sline);
    return *p;
}

static ast::c_enum const &parse_enum(lex_state &ls) {
    int eline = ls.line_number;
    ls.get();

    /* name is optional */
    bool named = false;
    std::string ename = "enum ";
    if (ls.t.token == TOK_NAME) {
        ename += ls.t.value_s;
        ls.get();
        named = true;
    } else {
        ename += ls.request_name();
    }

    int linenum = ls.line_number;

    auto mode_error = [&ls, named]() {
        if (named && (ls.mode() == PARSE_MODE_NOTCDEF)) {
            ls.syntax_error("enum declaration not allowed in this context");
        }
    };

    if (!test_next(ls, '{')) {
        auto *oldecl = ls.lookup(ename.c_str());
        if (!oldecl || (oldecl->obj_type() != ast::c_object_type::ENUM)) {
            mode_error();
            auto *p = new ast::c_enum{std::move(ename)};
            ls.store_decl(p, eline);
            return *p;
        }
        return oldecl->as<ast::c_enum>();
    }

    mode_error();

    std::vector<ast::c_enum::field> fields;

    while (ls.t.token != '}') {
        check(ls, TOK_NAME);
        std::string fname = ls.t.value_s;
        ls.get();
        if (ls.t.token == '=') {
            ls.get();
            auto exp = parse_cexpr(ls);
            ast::c_expr_type et;
            auto val = exp.eval(et, true);
            /* for now large types just get truncated */
            switch (et) {
                case ast::c_expr_type::INT: break;
                case ast::c_expr_type::UINT: val.i = val.u; break;
                case ast::c_expr_type::LONG: val.i = val.l; break;
                case ast::c_expr_type::ULONG: val.i = val.ul; break;
                case ast::c_expr_type::LLONG: val.i = val.ll; break;
                case ast::c_expr_type::ULLONG: val.i = val.ull; break;
                default:
                    ls.syntax_error("unsupported type");
                    break;
            }
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

    auto *oldecl = ls.lookup(ename.c_str());
    if (oldecl && (oldecl->obj_type() == ast::c_object_type::ENUM)) {
        auto &st = oldecl->as<ast::c_enum>();
        if (st.opaque()) {
            /* previous declaration was opaque; prevent redef errors */
            st.set_fields(std::move(fields));
            return st;
        }
    }

    auto *p = new ast::c_enum{std::move(ename), std::move(fields)};
    ls.store_decl(p, eline);
    return *p;
}

static void parse_decl(lex_state &ls) {
    int dline = ls.line_number;
    std::string dname;
    switch (ls.t.token) {
        case TOK_typedef: {
            /* syntax 1: typedef FROM TO; */
            ls.get();
            parse_typedef(ls, parse_type(ls, true, &dname), dname, dline);
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

    if (tp.type() != ast::C_BUILTIN_FUNC) {
        ls.store_decl(
            new ast::c_variable{std::move(dname), std::move(tp)}, dline
        );
        return;
    }

    /* FIXME: do not allocate two functions needlessly */
    auto &func = tp.function();
    ls.store_decl(new ast::c_function{
        std::move(dname), func.result(), func.params(), func.variadic()
    }, dline);
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
    try {
        lex_state ls{L, input, iend};
        /* read first token */
        ls.get();
        parse_decls(ls);
        ls.commit();
    } catch (lex_state_error const &e) {
        if (e.token > 0) {
            luaL_error(
                L, "input:%d: %s near '%s'", e.line_number, e.what(),
                token_to_str(e.token).c_str()
            );
        } else {
            luaL_error(L, "input:%d: %s", e.line_number, e.what());
        }
    }
}

ast::c_type parse_type(lua_State *L, char const *input, char const *iend) {
    if (!iend) {
        iend = input + strlen(input);
    }
    try {
        lex_state ls{L, input, iend, PARSE_MODE_NOTCDEF};
        ls.get();
        auto tp = parse_type(ls);
        ls.commit();
        return tp;
    } catch (lex_state_error const &e) {
        if (e.token > 0) {
            luaL_error(
                L, "%s near '%s'", e.what(), token_to_str(e.token).c_str()
            );
        } else {
            luaL_error(L, "%s", e.what());
        }
    }
    /* unreachable */
    return ast::c_type{ast::C_BUILTIN_INVALID, 0};
}

ast::c_expr_type parse_number(
    lua_State *L, lex_token_u &v, char const *input, char const *iend
) {
    if (!iend) {
        iend = input + strlen(input);
    }
    try {
        lex_state ls{L, input, iend, PARSE_MODE_NOTCDEF};
        ls.get();
        check(ls, TOK_INTEGER);
        v = ls.t.value;
        ls.commit();
        return ls.t.numtag;
    } catch (lex_state_error const &e) {
        if (e.token > 0) {
            luaL_error(
                L, "%s near '%s'", e.what(), token_to_str(e.token).c_str()
            );
        } else {
            luaL_error(L, "%s", e.what());
        }
    }
    /* unreachable */
    return ast::c_expr_type{};
}

} /* namespace parser */
