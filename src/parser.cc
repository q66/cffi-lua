#include <cstring>
#include <cctype>
#include <cstdint>
#include <cassert>

#include <string>

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

static thread_local util::str_map<int> keyword_map{};
static thread_local util::strbuf ls_buf{};

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

struct lex_state_error {
    int token;
    int line_number;
};

static thread_local lex_state_error ls_err{};

struct ls_error {};

enum parse_mode {
    PARSE_MODE_DEFAULT,
    PARSE_MODE_TYPEDEF,
    PARSE_MODE_NOTCDEF,
    PARSE_MODE_ATTRIB,
};

struct lex_state {
    lex_state() = delete;

    lex_state(
        lua_State *L, char const *str, const char *estr,
        int pmode = PARSE_MODE_DEFAULT, int paridx = -1
    ):
        p_mode(pmode), p_pidx(paridx), p_L(L), stream(str),
        send(estr), p_dstore{ast::decl_store::get_main(L)}
    {
        /* thread-local, initialize for parsing thread */
        init_kwmap();

        /* this should be enough that we should never have to resize it */
        ls_buf.clear();
        ls_buf.reserve(256);

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
            t = util::move(lahead);
            lahead.token = -1;
            return t.token;
        }
        return (t.token = lex(t));
    }

    int lookahead() {
        return (lahead.token = lex(t));
    }

    void lex_error(int tok, int linenum) {
        ls_err.token = tok;
        ls_err.line_number = linenum;
        throw ls_error{};
    }

    void lex_error(int tok) {
        lex_error(tok, line_number);
    }

    void syntax_error() {
        lex_error(t.token);
    }

    void store_decl(ast::c_object *obj, int lnum) {
        auto *old = p_dstore.add(obj);
        if (old) {
            ls_buf.clear();
            ls_buf.append('\'');
            ls_buf.append(old->name());
            ls_buf.append("' redefined");
            lex_error(-1, lnum);
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

    int request_name(char *buf, size_t bufsize) const {
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

    void param_maybe_name() {
        if (t.token != '$') {
            return;
        }
        ensure_pidx();
        size_t len;
        char const *str = lua_tolstring(p_L, p_pidx, &len);
        if (!str) {
            setbuf("name expected");
            syntax_error();
        }
        /* replace $ with name */
        t.token = TOK_NAME;
        setbuf(str, len);
        ++p_pidx;
    }

    /* FIXME: very preliminary, should support more stuff, more types */
    void param_maybe_expr() {
        if (t.token != '$') {
            return;
        }
        ensure_pidx();
        lua_Integer d = lua_tointeger(p_L, p_pidx);
        if (!d && !lua_isnumber(p_L, p_pidx)) {
            setbuf("integer expected");
            syntax_error();
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
    }

    ast::c_type param_get_type() {
        ensure_pidx();
        if (!luaL_testudata(p_L, p_pidx, lua::CFFI_CDATA_MT)) {
            setbuf("type expected");
            syntax_error();
        }
        auto ct = *lua::touserdata<ast::c_type>(p_L, p_pidx);
        get(); /* consume $ */
        ++p_pidx;
        return ct;
    }

    char const *getbuf() const {
        return &ls_buf[0];
    }

    void setbuf(char const *str, size_t len) {
        ls_buf.set(str, len);
    }

    void setbuf(char const *str) {
        ls_buf.set(str);
    }

    void appendbuf(char const *str, size_t len) {
        ls_buf.append(str, len);
    }

    void appendbuf(char const *str) {
        ls_buf.append(str);
    }

private:
    void ensure_pidx() {
        if ((p_pidx <= 0) || lua_isnone(p_L, p_pidx)) {
            setbuf("wrong number of type parameters");
            syntax_error();
        }
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
        setbuf("value out of bounds");
        lex_error(TOK_INTEGER);
        return ast::c_expr_type::INVALID;
    }

    template<size_t base, typename F, typename G>
    void read_int_core(F &&digf, G &&convf, lex_token &tok) {
        auto &lb = ls_buf.raw();
        lb.clear();
        do {
            lb.push_back(next_char());
        } while (digf(current));
        char const *numbeg = &ls_buf[0], *numend = &ls_buf[lb.size()];
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
                    setbuf("malformed integer");
                    lex_error(TOK_INTEGER);
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
                    setbuf("malformed integer");
                    lex_error(TOK_INTEGER);
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

    void read_escape(char &c) {
        next_char();
        switch (current) {
            case '\0':
                setbuf("unterminated escape sequence");
                lex_error(TOK_CHAR);
                return;
            case '\'':
            case '\"':
            case '\\':
            case '?':
                c = current;
                next_char();
                return;
            case 'e': /* extension */
                c = 0x1B;
                next_char();
                return;
            case 'a': c = '\a'; next_char(); return;
            case 'b': c = '\b'; next_char(); return;
            case 'f': c = '\f'; next_char(); return;
            case 'n': c = '\n'; next_char(); return;
            case 'r': c = '\r'; next_char(); return;
            case 't': c = '\t'; next_char(); return;
            case 'v': c = '\v'; next_char(); return;
            case 'x': {
                next_char();
                int c1 = current, c2 = upcoming();
                if (!isxdigit(c1) || !isxdigit(c2)) {
                    setbuf("malformed hex escape");
                    lex_error(TOK_CHAR);
                }
                c1 |= 32; c2 |= 32;
                c1 = (c1 >= 'a') ? (c1 - 'a' + 10) : (c1 - '0');
                c2 = (c2 >= 'a') ? (c2 - 'a' + 10) : (c2 - '0');
                c = char(c2 + (c1 * 16));
                next_char();
                next_char();
                return;
            }
            default:
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
                                setbuf("octal escape out of bounds");
                                lex_error(TOK_CHAR);
                            }
                            c = char(r);
                            return;
                        } else {
                            /* 2 octal digits */
                            c = char(c2 + (c1 * 8));
                            return;
                        }
                    } else {
                        /* 1 octal digit */
                        c = char(c1);
                        return;
                    }
                }
                setbuf("malformed escape sequence");
                lex_error(TOK_CHAR);
                return;
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
                    setbuf("unterminated comment");
                    syntax_error();
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
                    setbuf("unterminated literal");
                    lex_error(TOK_CHAR);
                } else if (current == '\\') {
                    read_escape(tok.value.c);
                } else {
                    tok.value.c = char(current);
                    next_char();
                }
                if (current != '\'') {
                    setbuf("unterminated literal");
                    lex_error(TOK_CHAR);
                }
                next_char();
                tok.numtag = ast::c_expr_type::CHAR;
                return TOK_CHAR;
            }
            /* string literal */
            case '\"': {
                auto &lb = ls_buf.raw();
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
                        setbuf("unterminated string");
                        lex_error(TOK_STRING);
                    }
                    if (current == '\\') {
                        char c = '\0';
                        read_escape(c);
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
                    auto &lb = ls_buf.raw();
                    lb.clear();
                    do {
                        lb.push_back(next_char());
                    } while (isalnum(current) || (current == '_'));
                    lb.push_back('\0');
                    /* could be a keyword? */
                    auto kwit = keyword_map.find(ls_buf.data());
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
        if (isprint(tok)) {
            buf[0] = char(tok);
            buf[1] = '\0';
        } else {
            snprintf(buf, 16, "char(%d)", tok);
        }
        return buf;
    }
    return tokens[tok - TOK_CUSTOM];
}

/* parser */

static void error_expected(lex_state &ls, int tok) {
    char buf[16 + sizeof("'' expected")];
    char *bufp = buf;
    *bufp++ = '\'';
    char const *tk = token_to_str(tok, bufp);
    auto tlen = strlen(tk);
    if (tk != bufp) {
        memcpy(bufp, tk, tlen);
    }
    bufp += tlen;
    memcpy(bufp, "' expected", sizeof("' expected"));
    ls.setbuf(buf);
    ls.syntax_error();
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
        char buf[16];
        ls_buf.clear();
        ls_buf.append('\'');
        ls_buf.append(token_to_str(what, buf));
        ls_buf.append("' expected (to close '");
        ls_buf.append(token_to_str(who, buf));
        ls_buf.append("' at line ");
        snprintf(buf, sizeof(buf), "%d", where);
        ls_buf.append(buf);
        ls_buf.append(')');
        ls.syntax_error();
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

static ast::c_expr parse_cexpr(lex_state &ls);
static ast::c_expr parse_cexpr_bin(lex_state &ls, int min_prec);

static ast::c_type parse_type(lex_state &ls, std::string *fpname = nullptr);

static ast::c_record const &parse_record(lex_state &ls, bool *newst = nullptr);
static ast::c_enum const &parse_enum(lex_state &ls);

static ast::c_expr parse_cexpr_simple(lex_state &ls) {
    auto unop = get_unop(ls.t.token);
    if (unop != ast::c_expr_unop::INVALID) {
        ls.get();
        auto exp = parse_cexpr_bin(ls, unprec);
        ast::c_expr unexp;
        unexp.type(ast::c_expr_type::UNARY);
        unexp.un.op = unop;
        unexp.un.expr = new ast::c_expr{util::move(exp)};
        return unexp;
    }
    /* FIXME: implement non-integer constants */
    if (ls.t.token == '$') {
        ls.param_maybe_expr();
    }
    switch (ls.t.token) {
        case TOK_INTEGER: {
            ast::c_expr ret;
            ret.type(ls.t.numtag);
            memcpy(&ret.val, &ls.t.value, sizeof(ls.t.value));
            ls.get();
            return ret;
        }
        case TOK_NAME: {
            ast::c_expr ret;
            auto *o = ls.lookup(ls.getbuf());
            if (!o || (o->obj_type() != ast::c_object_type::CONSTANT)) {
                ls_buf.prepend("unknown constant '");
                ls_buf.append('\'');
                ls.syntax_error();
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
                    ls.setbuf("unknown type");
                    ls.syntax_error();
                    break;
            }
            ret.val = ct.value();
            ls.get();
            return ret;
        }
        case TOK_true:
        case TOK_false: {
            ast::c_expr ret;
            ret.type(ast::c_expr_type::BOOL);
            ret.val.b = (ls.t.token == TOK_true);
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
                ret.type(ast::c_expr_type::ULONG);
                ret.val.ul = static_cast<unsigned long>(align);
            } else {
                ret.type(ast::c_expr_type::ULLONG);
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
                ret.type(ast::c_expr_type::ULONG);
                ret.val.ul = static_cast<unsigned long>(align);
            } else {
                ret.type(ast::c_expr_type::ULLONG);
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
            ls.setbuf("unexpected symbol");
            ls.syntax_error();
            break;
    }
    return ast::c_expr{}; /* unreachable */
}

static ast::c_expr parse_cexpr_bin(lex_state &ls, int min_prec) {
    auto lhs = parse_cexpr_simple(ls);
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
        ls.get();
        if (istern) {
            ast::c_expr texp = parse_cexpr(ls);
            check_next(ls, ':');
            ast::c_expr fexp = parse_cexpr_bin(ls, ifprec);
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
        ast::c_expr rhs = parse_cexpr_bin(ls, nprec);
        ast::c_expr bin;
        bin.type(ast::c_expr_type::BINARY);
        bin.bin.op = op;
        bin.bin.lhs = new ast::c_expr{util::move(lhs)};
        bin.bin.rhs = new ast::c_expr{util::move(rhs)};
        lhs = util::move(bin);
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
            ls.setbuf("invalid array size");
            ls.syntax_error();
            break;
    }
    if (sval < 0) {
        ls.setbuf("array size is negative");
        ls.syntax_error();
    }
    uval = sval;

done:
    using ULL = unsigned long long;
    if (uval > ULL(~size_t(0))) {
        ls.setbuf("array sie too big");
        ls.syntax_error();
    }
    return size_t(uval);
}

static uint32_t parse_cv(
    lex_state &ls, bool *tdef = nullptr, bool *extr = nullptr
) {
    uint32_t quals = 0;

    for (;;) switch (ls.t.token) {
        case TOK_const:
        case TOK___const__:
            if (quals & ast::C_CV_CONST) {
                ls.setbuf("duplicate const qualifier");
                ls.syntax_error();
                break;
            }
            ls.get();
            quals |= ast::C_CV_CONST;
            break;
        case TOK_volatile:
        case TOK___volatile__:
            if (quals & ast::C_CV_VOLATILE) {
                ls.setbuf("duplicate volatile qualifier");
                ls.syntax_error();
                break;
            }
            ls.get();
            quals |= ast::C_CV_VOLATILE;
            break;
        case TOK_typedef:
            if (!tdef) {
                return quals;
            }
            if (*tdef) {
                ls.setbuf("duplicate typedef qualifier");
                ls.syntax_error();
                break;
            }
            ls.get();
            *tdef = true;
            break;
        case TOK_extern:
            if (!extr) {
                return quals;
            }
            if (*extr) {
                ls.setbuf("duplicate extern qualifier");
                ls.syntax_error();
                break;
            }
            ls.get();
            *extr = true;
            break;
        default:
            return quals;
    }

    return quals;
}

static uint32_t parse_callconv_attrib(lex_state &ls) {
    if (ls.t.token != TOK___attribute__) {
        return ast::C_FUNC_DEFAULT;
    }
    int omod = ls.mode(PARSE_MODE_ATTRIB);
    ls.get();
    int ln = ls.line_number;
    check_next(ls, TOK_ATTRIBB);
    int conv = -1;
    check(ls, TOK_NAME);
    if (!strcmp(ls.getbuf(), "cdecl")) {
        conv = ast::C_FUNC_CDECL;
    } else if (!strcmp(ls.getbuf(), "fastcall")) {
        conv = ast::C_FUNC_FASTCALL;
    } else if (!strcmp(ls.getbuf(), "stdcall")) {
        conv = ast::C_FUNC_STDCALL;
    } else if (!strcmp(ls.getbuf(), "thiscall")) {
        conv = ast::C_FUNC_THISCALL;
    } else {
        ls.setbuf("invalid calling convention");
        ls.syntax_error();
    }
    ls.get();
    check_match(ls, TOK_ATTRIBE, TOK_ATTRIBB, ln);
    ls.mode(omod);
    return conv;
}

static uint32_t parse_callconv_ms(lex_state &ls) {
    switch (ls.t.token) {
        case TOK___cdecl:
            ls.get();
            return ast::C_FUNC_CDECL;
        case TOK___fastcall:
            ls.get();
            return ast::C_FUNC_FASTCALL;
        case TOK___stdcall:
            ls.get();
            return ast::C_FUNC_STDCALL;
        case TOK___thiscall:
            ls.get();
            return ast::C_FUNC_THISCALL;
        default:
            break;
    }
    return ast::C_FUNC_DEFAULT;
}

static util::vector<ast::c_param> parse_paramlist(lex_state &ls) {
    int linenum = ls.line_number;
    ls.get();

    util::vector<ast::c_param> params;

    if (ls.t.token == TOK_void) {
        if (ls.lookahead() == ')') {
            ls.get();
            goto done_params;
        }
    }

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
        auto pt = parse_type(ls, &pname);
        /* check if argument type can be passed by value */
        if (!pt.passable()) {
            ls_buf.clear();
            ls_buf.append('\'');
            pt.serialize(ls_buf);
            ls_buf.append("' cannot be passed by value");
            ls.syntax_error();
            break;
        }
        if (pname == "?") {
            pname.clear();
        }
        params.emplace_back(util::move(pname), util::move(pt));
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
        arrd{0}, cv{0}, flags{0}, is_term{false}, is_func{false},
        is_ref{false}
    {}
    ~plevel() {
        if (is_func) {
            using DT = util::vector<ast::c_param>;
            argl.~DT();
        }
    }
    plevel(plevel &&v):
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
        size_t arrd;
    };
    uint32_t cv: 2;
    uint32_t flags: 6;
    uint32_t cconv: 6;
    uint32_t is_term: 1;
    uint32_t is_func: 1;
    uint32_t is_ref: 1;
};

/* first we define a list containing 'levels'... each level denotes one
 * matched pair of parens, except the implicit default level which is always
 * added; new level is delimited by a sentinel value, and the elements past
 * the sentinel can specify pointers and references
 *
 * this is a thread local value; it could be defined within the parse_type_ptr
 * call but we don't want to constantly allocate and deallocate, so just create
 * it once per thread, its resources will be reused by subsequent and recursive
 * calls
*/
static thread_local util::vector<plevel> pcvq{};

/* this stack stores whatever parse_array below parses, the number of elements
 * per single parse_type_ptr call is stored above in the level struct; each
 * level with non-zero arrd will pop off arrd items
 */
struct arrdim {
    size_t size;
    uint32_t quals;
};
static thread_local util::vector<arrdim> dimstack{};

/* FIXME: when in var declarations, all components must be complete */
static size_t parse_array(lex_state &ls, int &flags) {
    flags = 0;
    size_t ndims = 0;
    if (ls.t.token != '[') {
        return ndims;
    }
    ls.get();
    auto cv = parse_cv(ls);
    if (ls.t.token == ']') {
        flags |= ast::C_TYPE_NOSIZE;
        dimstack.push_back({0, cv});
        ++ndims;
        ls.get();
    } else if (ls.t.token == '?') {
        /* FIXME: this should only be available in cdata creation contexts */
        flags |= ast::C_TYPE_VLA;
        dimstack.push_back({0, cv});
        ++ndims;
        ls.get();
        check_next(ls, ']');
    } else {
        dimstack.push_back({get_arrsize(ls, parse_cexpr(ls)), cv});
        ++ndims;
        check_next(ls, ']');
    }
    while (ls.t.token == '[') {
        ls.get();
        cv = parse_cv(ls);
        dimstack.push_back({get_arrsize(ls, parse_cexpr(ls)), cv});
        ++ndims;
        check_next(ls, ']');
    }
    return ndims;
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
static ast::c_type parse_type_ptr(
    lex_state &ls, ast::c_type tp, std::string *fpname, bool needn
) {
    /* our input is the left-side qualified type; that means constructs such
     * as 'const int' or 'unsigned long int const'
     */

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
     * first we save the size of the level list, that will denote the boundary
     * of this specific call - it will be the first index we can access from
     * here - this is because this function can be recursive, and we can't have
     * inner calls overwriting stuff that belongs to the outer calls
     */
    auto pidx = intptr_t(pcvq.size());
    bool nolev = true;
    /* normally we'd consume the '(', but remember, first level is implicit */
    goto newlevel;
    do {
        ls.get();
        nolev = false;
newlevel:
        /* create the sentinel */
        pcvq.emplace_back();
        pcvq.back().is_term = true;
        if (!nolev) {
            pcvq.back().cconv = parse_callconv_ms(ls);
        } else {
            pcvq.back().cconv = ast::C_FUNC_DEFAULT;
        }
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
    auto tidx = intptr_t(pcvq.size());
    /* the most basic special case when there are no (),
     * calling convention can go before the name
     */
    if (nolev) {
        pcvq[pidx].cconv = parse_callconv_ms(ls);
        if (pcvq[pidx].cconv == ast::C_FUNC_DEFAULT) {
            pcvq[pidx].cconv = parse_callconv_attrib(ls);
        }
    }
    /* if 'fpname' was passed, it means we might want to handle a named type
     * or declaration, with the name being optional or mandatory depending
     * on 'needn' - if name was optionally requested but not found, we write
     * a dummy value to tell the caller what happened
     */
    if (fpname) {
        ls.param_maybe_name();
        if (needn || (ls.t.token == TOK_NAME)) {
            /* we're in a context where name can be provided, e.g. if
             * parsing a typedef or a prototype, this will be the name
             */
            check(ls, TOK_NAME);
            *fpname = ls.getbuf();
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
    uint32_t prevconv = ast::C_FUNC_DEFAULT;
    for (intptr_t ridx = tidx - 1;;) {
        if (!pcvq[ridx].is_term) { /* skip non-sentinels */
            --ridx;
            continue;
        }
        if (ls.t.token == '(') {
            /* we know it's a paramlist, since all starting '(' of levels
             * are already consumed since before
             */
            auto argl = parse_paramlist(ls);
            auto &clev = pcvq[ridx];
            new (&clev.argl) util::vector<ast::c_param>(util::move(argl));
            clev.is_func = true;
            /* attribute style calling convention after paramlist */
            clev.cconv = parse_callconv_attrib(ls);
            if (clev.cconv == ast::C_FUNC_DEFAULT) {
                clev.cconv = prevconv;
            }
        } else if (ls.t.token == '[') {
            /* array dimensions may be multiple */
            int flags;
            pcvq[ridx].arrd = parse_array(ls, flags);
            pcvq[ridx].flags = flags;
        }
        if (!pcvq[ridx].is_func && (prevconv != ast::C_FUNC_DEFAULT)) {
            ls.setbuf("calling convention on non-function declaration");
            ls.syntax_error();
        }
        prevconv = pcvq[ridx].cconv;
        --ridx;
        /* special case of the implicit level, it's not present in syntax */
        if (ridx < pidx) {
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
    plevel *olev = &pcvq[pidx];
    for (intptr_t cidx = pidx + 1;; ++cidx) {
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
                ls.setbuf("references must be trailing");
                ls.syntax_error();
            }
            if (pcvq[cidx].is_ref) {
                tp.add_ref();
            } else {
                ast::c_type ntp{util::move(tp), pcvq[cidx].cv, ast::C_BUILTIN_PTR};
                tp = util::move(ntp);
            }
            ++cidx;
        }
        /* now attach the function or array or whatever */
        if (olev->is_func) {
            /* outer level has an arglist */
            uint32_t fflags = olev->cconv;
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
                ls_buf.clear();
                ls_buf.append('\'');
                tp.serialize(ls_buf);
                ls_buf.append("' cannot be passed by value");
                ls.syntax_error();
                break;
            }
            ast::c_function cf{util::move(tp), util::move(olev->argl), fflags};
            tp = ast::c_type{util::move(cf), 0};
        } else if (olev->arrd) {
            if (tp.vla() || tp.unbounded()) {
                ls.setbuf("only first bound of an array may have unknown size");
                ls.syntax_error();
            }
            while (olev->arrd) {
                size_t dim = dimstack.back().size;
                auto quals = dimstack.back().quals;
                dimstack.pop_back();
                --olev->arrd;
                ast::c_type atp{
                    util::move(tp), quals, dim,
                    (!olev->arrd ? olev->flags : uint32_t(0))
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
        ls.setbuf("void type in forbidden context");
        ls.syntax_error();
    }
    /* shrink it back to what it was, these resources can be reused later */
    pcvq.shrink(pidx);
    return tp;
}

enum type_signedness {
    TYPE_SIGNED = 1 << 0,
    TYPE_UNSIGNED = 1 << 1
};

static ast::c_type parse_typebase_core(lex_state &ls, bool *tdef, bool *extr) {
    /* left-side cv */
    uint32_t quals = parse_cv(ls, tdef, extr);
    uint32_t squals = 0;

    /* parameterized types */
    if (ls.t.token == '$') {
        auto ret = ls.param_get_type();
        ret.cv(quals);
        return ret;
    }

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
    } else if ((ls.t.token == TOK_struct) || (ls.t.token == TOK_union)) {
        return ast::c_type{&parse_record(ls), quals};
    } else if (ls.t.token == TOK_enum) {
        return ast::c_type{&parse_enum(ls), quals};
    }

qualified:
    if (ls.t.token == TOK_NAME) {
        /* typedef, struct, enum, var, etc. */
        auto *decl = ls.lookup(ls.getbuf());
        if (!decl) {
            ls_buf.prepend("undeclared symbol '");
            ls_buf.append('\'');
            ls.syntax_error();
        }
        switch (decl->obj_type()) {
            case ast::c_object_type::TYPEDEF: {
                ls.get();
                ast::c_type tp{decl->as<ast::c_typedef>().type()};
                /* merge qualifiers */
                tp.cv(quals);
                return tp;
            }
            case ast::c_object_type::RECORD: {
                ls.get();
                auto &tp = decl->as<ast::c_record>();
                return ast::c_type{&tp, quals};
            }
            case ast::c_object_type::ENUM: {
                ls.get();
                auto &tp = decl->as<ast::c_enum>();
                return ast::c_type{&tp, quals};
            }
            default: {
                ls_buf.prepend("symbol '");
                ls_buf.append("' is not a type");
                ls.syntax_error();
                break;
            }
        }
    } else switch (ls.t.token) {
        /* may be a builtin type */
        case TOK_void:
            cbt = ast::C_BUILTIN_VOID;
            goto btype;
        case TOK_int8_t:
            cbt = ast::builtin_v<int8_t>;
            goto btype;
        case TOK_int16_t:
            cbt = ast::builtin_v<int16_t>;
            goto btype;
        case TOK_int32_t:
            cbt = ast::builtin_v<int32_t>;
            goto btype;
        case TOK_int64_t:
            cbt = ast::builtin_v<int64_t>;
            goto btype;
        case TOK_uint8_t:
            cbt = ast::builtin_v<uint8_t>;
            goto btype;
        case TOK_uint16_t:
            cbt = ast::builtin_v<uint16_t>;
            goto btype;
        case TOK_uint32_t:
            cbt = ast::builtin_v<uint32_t>;
            goto btype;
        case TOK_uint64_t:
            cbt = ast::builtin_v<uint64_t>;
            goto btype;
        case TOK_uintptr_t:
            cbt = ast::builtin_v<uintptr_t>;
            goto btype;
        case TOK_intptr_t:
            cbt = ast::builtin_v<intptr_t>;
            goto btype;
        case TOK_ptrdiff_t:
            cbt = ast::builtin_v<ptrdiff_t>;
            goto btype;
        case TOK_ssize_t:
            cbt = ast::builtin_v<util::add_sign_t<size_t>>;
            goto btype;
        case TOK_size_t:
            cbt = ast::builtin_v<size_t>;
            goto btype;
        case TOK_va_list:
        case TOK___builtin_va_list:
        case TOK___gnuc_va_list:
            cbt = ast::C_BUILTIN_VA_LIST;
            goto btype;
        case TOK_time_t:   cbt = ast::builtin_v<time_t>;   goto btype;
        case TOK_wchar_t:  cbt = ast::builtin_v<wchar_t>;  goto btype;
        case TOK_char16_t: cbt = ast::builtin_v<char16_t>; goto btype;
        case TOK_char32_t: cbt = ast::builtin_v<char32_t>; goto btype;
        case TOK_float:    cbt = ast::C_BUILTIN_FLOAT;     goto btype;
        case TOK_double:   cbt = ast::C_BUILTIN_DOUBLE;    goto btype;
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
            ls.setbuf("type name expected");
            ls.syntax_error();
            break;
    }

newtype:
    assert(cbt != ast::C_BUILTIN_INVALID);
    return ast::c_type{cbt, quals};
}

static ast::c_type parse_typebase(
    lex_state &ls, bool *tdef = nullptr, bool *extr = nullptr
) {
    auto tp = parse_typebase_core(ls, tdef, extr);
    /* right-side cv that can always apply */
    tp.cv(parse_cv(ls, tdef, extr));
    return tp;
}

static ast::c_type parse_type(lex_state &ls, std::string *fpn) {
    return parse_type_ptr(ls, parse_typebase(ls), fpn, false);
}

static ast::c_record const &parse_record(lex_state &ls, bool *newst) {
    int sline = ls.line_number;
    bool is_uni = (ls.t.token == TOK_union);
    ls.get(); /* struct/union keyword */

    /* name is optional */
    bool named = false;
    std::string sname = is_uni ? "union " : "struct ";
    ls.param_maybe_name();
    if (ls.t.token == TOK_NAME) {
        sname += ls.getbuf();
        ls.get();
        named = true;
    } else {
        char buf[32];
        auto wn = ls.request_name(buf, sizeof(buf));
        assert((wn > 0) && (wn < int(sizeof(buf))));
        sname += static_cast<char const *>(buf);
    }

    int linenum = ls.line_number;

    auto mode_error = [&ls, named]() {
        if (named && (ls.mode() == PARSE_MODE_NOTCDEF)) {
            ls.setbuf("struct declaration not allowed in this context");
            ls.syntax_error();
        }
    };

    /* opaque */
    if (!test_next(ls, '{')) {
        auto *oldecl = ls.lookup(sname.c_str());
        if (!oldecl || (oldecl->obj_type() != ast::c_object_type::RECORD)) {
            mode_error();
            /* different type or not stored yet, raise error or store */
            auto *p = new ast::c_record{util::move(sname), is_uni};
            ls.store_decl(p, sline);
            return *p;
        }
        return oldecl->as<ast::c_record>();
    }

    mode_error();

    util::vector<ast::c_record::field> fields;

    while (ls.t.token != '}') {
        ast::c_type tpb{ast::C_BUILTIN_INVALID, 0};
        if ((ls.t.token == TOK_struct) || (ls.t.token == TOK_union)) {
            bool transp = false;
            auto &st = parse_record(ls, &transp);
            if (transp && test_next(ls, ';')) {
                fields.emplace_back(std::string{}, ast::c_type{&st, 0});
                continue;
            }
            tpb = ast::c_type{&st, parse_cv(ls)};
        } else {
            tpb = parse_typebase(ls);
        }
        bool flexible = false;
        do {
            std::string fpn;
            auto tp = parse_type_ptr(ls, tpb, &fpn, false);
            if (fpn == "?") {
                /* nameless field declarations do nothing */
                goto field_end;
            }
            flexible = tp.unbounded();
            fields.emplace_back(util::move(fpn), util::move(tp));
            /* unbounded array must be the last in the list */
            if (flexible) {
                break;
            }
        } while (test_next(ls, ','));
field_end:
        check_next(ls, ';');
        /* unbounded array must be the last in the struct */
        if (flexible) {
            break;
        }
    }

    check_match(ls, '}', '{', linenum);

    auto *oldecl = ls.lookup(sname.c_str());
    if (oldecl && (oldecl->obj_type() == ast::c_object_type::RECORD)) {
        auto &st = oldecl->as<ast::c_record>();
        if (st.opaque()) {
            /* previous declaration was opaque; prevent redef errors */
            st.set_fields(util::move(fields));
            if (newst) {
                *newst = true;
            }
            return st;
        }
    }

    if (newst) {
        *newst = true;
    }
    auto *p = new ast::c_record{util::move(sname), util::move(fields), is_uni};
    ls.store_decl(p, sline);
    return *p;
}

static ast::c_enum const &parse_enum(lex_state &ls) {
    int eline = ls.line_number;
    ls.get();

    /* name is optional */
    bool named = false;
    std::string ename = "enum ";
    ls.param_maybe_name();
    if (ls.t.token == TOK_NAME) {
        ename += ls.getbuf();
        ls.get();
        named = true;
    } else {
        char buf[32];
        auto wn = ls.request_name(buf, sizeof(buf));
        assert((wn > 0) && (wn < int(sizeof(buf))));
        ename += static_cast<char const *>(buf);
    }

    int linenum = ls.line_number;

    auto mode_error = [&ls, named]() {
        if (named && (ls.mode() == PARSE_MODE_NOTCDEF)) {
            ls.setbuf("enum declaration not allowed in this context");
            ls.syntax_error();
        }
    };

    if (!test_next(ls, '{')) {
        auto *oldecl = ls.lookup(ename.c_str());
        if (!oldecl || (oldecl->obj_type() != ast::c_object_type::ENUM)) {
            mode_error();
            auto *p = new ast::c_enum{util::move(ename)};
            ls.store_decl(p, eline);
            return *p;
        }
        return oldecl->as<ast::c_enum>();
    }

    mode_error();

    util::vector<ast::c_enum::field> fields;

    while (ls.t.token != '}') {
        int eln = ls.line_number;
        ls.param_maybe_name();
        check(ls, TOK_NAME);
        std::string fname = ls.getbuf();
        ls.get();
        if (ls.t.token == '=') {
            ls.get();
            eln = ls.line_number;
            auto exp = parse_cexpr(ls);
            ast::c_expr_type et;
            auto val = exp.eval(et, true);
            /* for now large types just get truncated */
            switch (et) {
                case ast::c_expr_type::INT: break;
                case ast::c_expr_type::UINT: val.i = val.u; break;
                case ast::c_expr_type::LONG: val.i = int(val.l); break;
                case ast::c_expr_type::ULONG: val.i = int(val.ul); break;
                case ast::c_expr_type::LLONG: val.i = int(val.ll); break;
                case ast::c_expr_type::ULLONG: val.i = int(val.ull); break;
                default:
                    ls.setbuf("unsupported type");
                    ls.syntax_error();
                    break;
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
        ls.store_decl(p, eln);
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
            st.set_fields(util::move(fields));
            return st;
        }
    }

    auto *p = new ast::c_enum{util::move(ename), util::move(fields)};
    ls.store_decl(p, eline);
    return *p;
}

static void parse_decl(lex_state &ls) {
    int dline = ls.line_number;
    uint32_t cconv = parse_callconv_attrib(ls);
    bool tdef = false, extr = false;
    auto tpb = parse_typebase(ls, &tdef, &extr);
    bool first = true;
    do {
        std::string dname;
        int oldmode = 0;
        if (tdef) {
            oldmode = ls.mode(PARSE_MODE_TYPEDEF);
        }
        auto tp = parse_type_ptr(ls, tpb, &dname, !first);
        first = false;
        if (cconv != ast::C_FUNC_DEFAULT) {
            if (tp.type() != ast::C_BUILTIN_FUNC) {
                ls.setbuf("calling convention on non-function declaration");
                ls.syntax_error();
            }
            auto *func = const_cast<ast::c_function *>(&tp.function());
            func->callconv(cconv);
        }
        if (tdef) {
            ls.mode(oldmode);
            if (dname != "?") {
                /* store if the name is non-empty, if it's empty there is no
                 * way to access the type and it'd be unique either way
                 */
                ls.store_decl(
                    new ast::c_typedef{util::move(dname), util::move(tp)}, dline
                );
                continue;
            } else {
                /* unnamed typedef must not be a list */
                break;
            }
        } else if (dname == "?") {
            /* if no name is permitted, it must be the only one */
            break;
        }
        std::string sym;
        /* symbol redirection */
        if (test_next(ls, TOK___asm__)) {
            int lnum = ls.line_number;
            check_next(ls, '(');
            check(ls, TOK_STRING);
            if (ls.getbuf()[0] == '\0') {
                ls.setbuf("empty symbol name");
                ls.syntax_error();
            }
            sym = ls.getbuf();
            ls.get();
            check_match(ls, ')', '(', lnum);
        }
        ls.store_decl(new ast::c_variable{
            util::move(dname), util::move(sym), util::move(tp)
        }, dline);
    } while (test_next(ls, ','));
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

static void parse_err(lua_State *L) {
    luaL_where(L, 1);
    lua_insert(L, -2);
    lua_concat(L, 2);
    lua_error(L);
}

void parse(lua_State *L, char const *input, char const *iend, int paridx) {
    if (!iend) {
        iend = input + strlen(input);
    }
    {
        lex_state ls{L, input, iend, PARSE_MODE_DEFAULT, paridx};
        try {
            /* read first token */
            ls.get();
            parse_decls(ls);
            ls.commit();
            return;
        } catch (ls_error) {
            if (ls_err.token > 0) {
                char buf[16];
                lua_pushfstring(
                    L, "input:%d: %s near '%s'", ls_err.line_number,
                    ls.getbuf(), token_to_str(ls_err.token, buf)
                );
            } else {
                lua_pushfstring(
                    L, "input:%d: %s", ls_err.line_number, ls.getbuf()
                );
            }
            goto lerr;
        }
    }
lerr:
    parse_err(L);
}

ast::c_type parse_type(
    lua_State *L, char const *input, char const *iend, int paridx
) {
    if (!iend) {
        iend = input + strlen(input);
    }
    {
        lex_state ls{L, input, iend, PARSE_MODE_NOTCDEF, paridx};
        try {
            ls.get();
            auto tp = parse_type(ls);
            ls.commit();
            return tp;
        } catch (ls_error) {
            if (ls_err.token > 0) {
                char buf[16];
                lua_pushfstring(
                    L, "%s near '%s'", ls.getbuf(),
                    token_to_str(ls_err.token, buf)
                );
            } else {
                lua_pushfstring(L, "%s", ls.getbuf());
            }
            goto lerr;
        }
    }
lerr:
    parse_err(L);
    /* unreachable */
    return ast::c_type{ast::C_BUILTIN_INVALID, 0};
}

ast::c_expr_type parse_number(
    lua_State *L, ast::c_value &v, char const *input, char const *iend
) {
    if (!iend) {
        iend = input + strlen(input);
    }
    {
        lex_state ls{L, input, iend, PARSE_MODE_NOTCDEF};
        try {
            ls.get();
            check(ls, TOK_INTEGER);
            v = ls.t.value;
            ls.commit();
            return ls.t.numtag;
        } catch (ls_error) {
            if (ls_err.token > 0) {
                char buf[16];
                lua_pushfstring(
                    L, "%s near '%s'", ls.getbuf(),
                    token_to_str(ls_err.token, buf)
                );
            } else {
                lua_pushfstring(L, "%s", ls.getbuf());
            }
            goto lerr;
        }
    }
lerr:
    parse_err(L);
    /* unreachable */
    return ast::c_expr_type{};
}

} /* namespace parser */
