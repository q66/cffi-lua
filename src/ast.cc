#include <cassert>
#include <cstdint>

#include "platform.hh"
#include "util.hh"
#include "ast.hh"
#include "ffi.hh"

namespace ast {

/* This unholy spaghetti implements integer promotions as well as conversion
 * rules in arithmetic operations etc. as the C standard defines it...
 */

static void promote_int(c_value &v, c_expr_type &et) {
    switch (et) {
        case c_expr_type::BOOL:
            v.i = int(v.b); et = c_expr_type::INT; break;
        case c_expr_type::CHAR:
            v.i = int(v.c); et = c_expr_type::INT; break;
        default:
            break;
    }
}

/* only integers have ranks but for our purposes this is fine */
static int get_rank(lua_State *L, c_expr_type type) WARN_UNUSED_RET;
static int get_rank(lua_State *L, c_expr_type type) {
    switch (type) {
        case c_expr_type::INT: return 0;
        case c_expr_type::UINT: return 0;
        case c_expr_type::LONG: return 1;
        case c_expr_type::ULONG: return 1;
        case c_expr_type::LLONG: return 2;
        case c_expr_type::ULLONG: return 2;
        case c_expr_type::FLOAT: return 3;
        case c_expr_type::DOUBLE: return 4;
        case c_expr_type::LDOUBLE: return 5;
        default:
            lua_pushliteral(L, "invalid type for operand");
            break;
    }
    return -1;
}

static bool is_signed(c_expr_type type) WARN_UNUSED_RET;
static bool is_signed(c_expr_type type) {
    switch (type) {
        case c_expr_type::CHAR:
            return util::is_signed<char>::value;
        case c_expr_type::INT:
        case c_expr_type::LONG:
        case c_expr_type::LLONG:
        case c_expr_type::FLOAT:
        case c_expr_type::DOUBLE:
        case c_expr_type::LDOUBLE:
            return true;
        default:
            break;
    }
    return false;
}

static bool convert_bin(
    lua_State *L, c_value &lval, c_expr_type &let, c_value &rval,
    c_expr_type &ret
) WARN_UNUSED_RET;

static bool convert_bin(
    lua_State *L, c_value &lval, c_expr_type &let, c_value &rval,
    c_expr_type &ret
) {
    /* same types, likely case, bail out early */
    if (let == ret) {
        return true;
    }

    int lrank = get_rank(L, let);
    int rrank = get_rank(L, ret);
    if ((lrank < 0) || (rrank < 0)) {
        return false;
    }

    /* if right operand is higher ranked, treat it as left operand */
    if (rrank > lrank) {
        return convert_bin(L, rval, ret, lval, let);
    }

#define CONVERT_RVAL(lv) { \
    using LT = decltype(rval.lv); \
    switch (ret) { \
        case c_expr_type::DOUBLE: rval.lv = LT(rval.d); break; \
        case c_expr_type::FLOAT: rval.lv = LT(rval.f); break; \
        case c_expr_type::INT: rval.lv = LT(rval.i); break; \
        case c_expr_type::UINT: rval.lv = LT(rval.u); break; \
        case c_expr_type::LONG: rval.lv = LT(rval.l); break; \
        case c_expr_type::ULONG: rval.lv = LT(rval.ul); break; \
        case c_expr_type::LLONG: rval.lv = LT(rval.ll); break; \
        case c_expr_type::ULLONG: rval.lv = LT(rval.ull); break; \
        default: break;  \
    } \
}

    /* at this point it's guaranteed that left rank is higer or equal */

    /* left operand is a float, convert to it */
    switch (let) {
        case c_expr_type::LDOUBLE:
            CONVERT_RVAL(ld); ret = let; return true;
        case c_expr_type::DOUBLE:
            CONVERT_RVAL(d); ret = let; return true;
        case c_expr_type::FLOAT:
            CONVERT_RVAL(f); ret = let; return true;
        default:
            break;
    }

    /* both operands are integers */

    bool lsig = is_signed(let);
    bool rsig = is_signed(ret);

    /* same signedness, convert lower ranked type to higher ranked */
    if (lsig == rsig) {
        switch (let) {
            case c_expr_type::ULLONG:
                CONVERT_RVAL(ull); ret = let; return true;
            case c_expr_type::LLONG:
                CONVERT_RVAL(ll); ret = let; return true;
            case c_expr_type::ULONG:
                CONVERT_RVAL(ul); ret = let; return true;
            case c_expr_type::LONG:
                CONVERT_RVAL(l); ret = let; return true;
            case c_expr_type::UINT:
                CONVERT_RVAL(u); ret = let; return true;
            case c_expr_type::INT:
                CONVERT_RVAL(i); ret = let; return true;
            default:
                break;
        }
        LUA_BUG_MSG(L, "unreachable code");
        return false;
    }

    /* unsigned type has greater or equal rank */
    if (rsig) {
        switch (let) {
            case c_expr_type::ULLONG:
                CONVERT_RVAL(ull); ret = let; return true;
            case c_expr_type::ULONG:
                CONVERT_RVAL(ul); ret = let; return true;
            case c_expr_type::UINT:
                CONVERT_RVAL(u); ret = let; return true;
            default:
                break;
        }
        LUA_BUG_MSG(L, "unreachable code");
        return false;
    }

#define CONVERT_RVAL_BOUNDED(lv) \
    switch (ret) { \
        case c_expr_type::ULONG: \
            if (sizeof(unsigned long) < sizeof(lval.lv)) { \
                rval.lv = rval.ul; \
                ret = let; \
                return true; \
            } \
            break; \
        case c_expr_type::UINT: \
            if (sizeof(unsigned int) < sizeof(lval.lv)) { \
                rval.lv = rval.u; \
                ret = let; \
                return true; \
            } \
            break; \
        default: \
            break; \
    }

    /* at this point left operand is always signed */

    /* try to fit right operand into it (may work if left has greater rank) */
    switch (let) {
        case c_expr_type::LLONG:
            CONVERT_RVAL_BOUNDED(ll); break;
        case c_expr_type::LONG:
            CONVERT_RVAL_BOUNDED(l); break;
        case c_expr_type::INT:
            break;
        default:
            break;
    }

#undef CONVERT_RVAL_BOUNDED

    /* does not fit; in that case, convert both to unsigned version of left */
     switch (let) {
        case c_expr_type::LLONG:
            lval.ull = lval.ll;
            CONVERT_RVAL(ull);
            let = ret = c_expr_type::ULLONG;
            return true;
        case c_expr_type::LONG:
            lval.ul = lval.l;
            CONVERT_RVAL(ul);
            let = ret = c_expr_type::ULONG;
            return true;
        case c_expr_type::INT:
            lval.u = lval.i;
            CONVERT_RVAL(u);
            let = ret = c_expr_type::UINT;
            return true;
        default:
            break;
    }

#undef CONVERT_RVAL

    LUA_BUG_MSG(L, "unreachable code");
    return false;
}

static bool eval_unary(
    lua_State *L, c_value &baseval, c_expr const &e, c_expr_type &et
) WARN_UNUSED_RET;

static bool eval_unary(
    lua_State *L, c_value &baseval, c_expr const &e, c_expr_type &et
) {
    if (!e.un.expr->eval(L, baseval, et, false)) {
        return false;
    }
    switch (e.un.op) {
        case c_expr_unop::UNP:
            promote_int(baseval, et);
            switch (et) {
                case c_expr_type::INT:
                case c_expr_type::UINT:
                case c_expr_type::LONG:
                case c_expr_type::ULONG:
                case c_expr_type::LLONG:
                case c_expr_type::ULLONG:
                    return true;
                default: break;
            }
            lua_pushliteral(L, "invalid type for +(expr)");
            return false;
        case c_expr_unop::UNM:
            promote_int(baseval, et);
            switch (et) {
                case c_expr_type::INT: baseval.i = -baseval.i; return true;
                case c_expr_type::UINT:
                    if (baseval.u) {
                        baseval.u = UINT_MAX - baseval.u + 1;
                    }
                    return true;
                case c_expr_type::LONG: baseval.l = -baseval.l; return true;
                case c_expr_type::ULONG:
                    if (baseval.ul) {
                        baseval.ul = ULONG_MAX - baseval.ul + 1;
                    }
                    return true;
                case c_expr_type::LLONG: baseval.ll = -baseval.ll; return true;
                case c_expr_type::ULLONG:
                    if (baseval.ull) {
                        baseval.ull = ULLONG_MAX - baseval.ull + 1;
                    }
                    return true;
                default: break;
            }
            lua_pushliteral(L, "invalid type for -(expr)");
            return false;
        case c_expr_unop::NOT:
            switch (et) {
                case c_expr_type::BOOL: baseval.b = !baseval.b; return true;
                case c_expr_type::CHAR: baseval.c = !baseval.c; return true;
                case c_expr_type::INT: baseval.i = !baseval.i; return true;
                case c_expr_type::UINT: baseval.u = !baseval.u; return true;
                case c_expr_type::LONG: baseval.l = !baseval.l; return true;
                case c_expr_type::ULONG: baseval.ul = !baseval.ul; return true;
                case c_expr_type::LLONG: baseval.ll = !baseval.ll; return true;
                case c_expr_type::ULLONG:
                    baseval.ull = !baseval.ull; return true;
                default: break;
            }
            lua_pushliteral(L, "invalid type for !(expr)");
            return false;
        case c_expr_unop::BNOT:
            promote_int(baseval, et);
            switch (et) {
                case c_expr_type::INT: baseval.i = ~baseval.i; return true;
                case c_expr_type::UINT: baseval.u = ~baseval.u; return true;
                case c_expr_type::LONG: baseval.l = ~baseval.l; return true;
                case c_expr_type::ULONG: baseval.ul = ~baseval.ul; return true;
                case c_expr_type::LLONG: baseval.ll = ~baseval.ll; return true;
                case c_expr_type::ULLONG:
                    baseval.ull = ~baseval.ull; return true;
                default: break;
            }
            lua_pushliteral(L, "invalid type for ~(expr)");
            return false;
        default:
            break;
    }
    LUA_BUG_MSG(L, "unreachable code");
    return false;
}

static bool eval_binary(
    lua_State *L, c_value &retv, c_expr const &e, c_expr_type &et
) WARN_UNUSED_RET;

static bool eval_binary(
    lua_State *L, c_value &retv, c_expr const &e, c_expr_type &et
) {
    c_expr_type let, ret;
    c_value lval, rval;
    if (!e.bin.lhs->eval(L, lval, let, false)) {
        return false;
    }
    if (!e.bin.rhs->eval(L, rval, ret, false)) {
        return false;
    }

#define BINOP_CASE(opn, op) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        if (!convert_bin(L, lval, let, rval, ret)) { \
            return false; \
        } \
        et = let; \
        switch (let) { \
            case c_expr_type::INT: retv.i = lval.i op rval.i; break; \
            case c_expr_type::UINT: retv.u = lval.u op rval.u; break; \
            case c_expr_type::LONG: retv.l = lval.l op rval.l; break; \
            case c_expr_type::ULONG: retv.ul = lval.ul op rval.ul; break; \
            case c_expr_type::LLONG: retv.ll = lval.ll op rval.ll; break; \
            case c_expr_type::ULLONG: retv.ull = lval.ull op rval.ull; break; \
            case c_expr_type::FLOAT: retv.f = lval.f op rval.f; break; \
            case c_expr_type::DOUBLE: retv.d = lval.d op rval.d; break; \
            case c_expr_type::LDOUBLE: retv.ld = lval.ld op rval.ld; break; \
            default: \
                lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
                return false; \
        } \
        break;

#define CMP_BOOL_CASE(opn, op) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        if (!convert_bin(L, lval, let, rval, ret)) { \
            return false; \
        } \
        et = c_expr_type::BOOL; \
        switch (let) { \
            case c_expr_type::INT: retv.b = lval.i op rval.i; break; \
            case c_expr_type::UINT: retv.b = lval.u op rval.u; break; \
            case c_expr_type::LONG: retv.b = lval.l op rval.l; break; \
            case c_expr_type::ULONG: retv.b = lval.ul op rval.ul; break; \
            case c_expr_type::LLONG: retv.b = lval.ll op rval.ll; break; \
            case c_expr_type::ULLONG: retv.b = lval.ull op rval.ull; break; \
            case c_expr_type::FLOAT: retv.b = lval.f op rval.f; break; \
            case c_expr_type::DOUBLE: retv.b = lval.d op rval.d; break; \
            case c_expr_type::LDOUBLE: retv.b = lval.ld op rval.ld; break; \
            default: \
                lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
                return false; \
        } \
        break;

#define BINOP_CASE_NOFLT(opn, op) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        if (!convert_bin(L, lval, let, rval, ret)) { \
            return false; \
        } \
        et = let; \
        switch (let) { \
            case c_expr_type::INT: retv.i = lval.i op rval.i; break; \
            case c_expr_type::UINT: retv.u = lval.u op rval.u; break; \
            case c_expr_type::LONG: retv.l = lval.l op rval.l; break; \
            case c_expr_type::ULONG: retv.ul = lval.ul op rval.ul; break; \
            case c_expr_type::LLONG: retv.ll = lval.ll op rval.ll; break; \
            case c_expr_type::ULLONG: retv.ull = lval.ull op rval.ull; break; \
            case c_expr_type::FLOAT: \
            case c_expr_type::DOUBLE: \
            case c_expr_type::LDOUBLE: \
                lua_pushliteral(L, "operator " #op " cannot be applied to floating point types"); \
                return false; \
            default: \
                lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
                return false; \
        } \
        break;

#define SHIFT_CASE_INNER(fn, op, nop) \
    /* shift by negative number is undefined in C, so define it as */ \
    /* shifting in the other direction; this works like lua 5.3    */ \
    switch (ret) { \
        case c_expr_type::INT: \
            if (rval.i < 0) { \
                retv.fn = lval.fn nop -rval.i; \
            } else { \
                retv.fn = lval.fn op rval.i; \
            } \
            break; \
        case c_expr_type::UINT: retv.fn = lval.fn op rval.u; break; \
        case c_expr_type::LONG: \
            if (rval.l < 0) { \
                retv.fn = lval.fn nop -rval.l; \
            } else { \
                retv.fn = lval.fn op rval.l; \
            } \
            break; \
        case c_expr_type::ULONG: retv.fn = lval.fn op rval.u; break; \
        case c_expr_type::LLONG: \
            if (rval.ll < 0) { \
                retv.fn = lval.fn nop -rval.ll; \
            } else { \
                retv.fn = lval.fn op rval.ll; \
            } \
            break; \
        case c_expr_type::ULLONG: retv.fn = lval.fn op rval.ull; break; \
        default: \
            lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
            return false; \
    }

#define SHIFT_CASE(opn, op, nop) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        et = let; \
        switch (let) { \
            case c_expr_type::INT: SHIFT_CASE_INNER(i, op, nop); break; \
            case c_expr_type::UINT: SHIFT_CASE_INNER(u, op, nop); break; \
            case c_expr_type::LONG: SHIFT_CASE_INNER(l, op, nop); break; \
            case c_expr_type::ULONG: SHIFT_CASE_INNER(ul, op, nop); break; \
            case c_expr_type::LLONG: SHIFT_CASE_INNER(ll, op, nop); break; \
            case c_expr_type::ULLONG: SHIFT_CASE_INNER(ull, op, nop); break; \
            default: \
                lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
                return false; \
        } \
        break;

#define BOOL_CASE_INNER(lv, op) \
    switch (ret) { \
        case c_expr_type::INT: retv.b = lv op rval.i; break; \
        case c_expr_type::UINT: retv.b = lv op rval.u; break; \
        case c_expr_type::LONG: retv.b = lv op rval.l; break; \
        case c_expr_type::ULONG: retv.b = lv op rval.ul; break; \
        case c_expr_type::LLONG: retv.b = lv op rval.ll; break; \
        case c_expr_type::ULLONG: retv.b = lv op rval.ull; break; \
        case c_expr_type::FLOAT: retv.b = lv op rval.f; break; \
        case c_expr_type::DOUBLE: retv.b = lv op rval.d; break; \
        case c_expr_type::LDOUBLE: retv.b = lv op rval.ld; break; \
        case c_expr_type::STRING: retv.b = lv op true; break; \
        case c_expr_type::CHAR: retv.b = lv op rval.c; break; \
        case c_expr_type::NULLPTR: retv.b = lv op nullptr; break; \
        case c_expr_type::BOOL: retv.b = lv op rval.b; break; \
        default: \
            lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
            return false; \
    }

#define BOOL_CASE(opn, op) \
    case c_expr_binop::opn: \
        et = c_expr_type::BOOL; \
        switch (let) { \
            case c_expr_type::INT: BOOL_CASE_INNER(lval.i, op); break; \
            case c_expr_type::UINT: BOOL_CASE_INNER(lval.u, op); break; \
            case c_expr_type::LONG: BOOL_CASE_INNER(lval.l, op); break; \
            case c_expr_type::ULONG: BOOL_CASE_INNER(lval.ul, op); break; \
            case c_expr_type::LLONG: BOOL_CASE_INNER(lval.ll, op); break; \
            case c_expr_type::ULLONG: BOOL_CASE_INNER(lval.ull, op); break; \
            case c_expr_type::FLOAT: BOOL_CASE_INNER(lval.f, op); break; \
            case c_expr_type::DOUBLE: BOOL_CASE_INNER(lval.d, op); break; \
            case c_expr_type::LDOUBLE: BOOL_CASE_INNER(lval.ld, op); break; \
            case c_expr_type::STRING: BOOL_CASE_INNER(true, op); break; \
            case c_expr_type::CHAR: BOOL_CASE_INNER(lval.c, op); break; \
            case c_expr_type::NULLPTR: BOOL_CASE_INNER(nullptr, op); break; \
            case c_expr_type::BOOL: BOOL_CASE_INNER(lval.b, op); break; \
            default: \
                lua_pushliteral(L, "invalid type(s) for (expr1 " #op " expr2)"); \
                return false; \
        } \
        break;

    switch (e.bin.op) {
        BINOP_CASE(ADD, +)
        BINOP_CASE(SUB, -)
        BINOP_CASE(MUL, *)
        BINOP_CASE(DIV, /)
        BINOP_CASE_NOFLT(MOD, %)

        CMP_BOOL_CASE(EQ, ==)
        CMP_BOOL_CASE(NEQ, !=)
        CMP_BOOL_CASE(GT, >)
        CMP_BOOL_CASE(LT, <)
        CMP_BOOL_CASE(GE, >=)
        CMP_BOOL_CASE(LE, <=)

        BOOL_CASE(AND, &&)
        BOOL_CASE(OR, ||)

        BINOP_CASE_NOFLT(BAND, &)
        BINOP_CASE_NOFLT(BOR, |)
        BINOP_CASE_NOFLT(BXOR, ^)
        SHIFT_CASE(LSH, <<, >>)
        SHIFT_CASE(RSH, >>, <<)

        default:
            LUA_BUG_MSG(L, "unhandled operator ");
            lua_pushfstring(L, "%d", int(e.bin.op));
            lua_concat(L, 2);
            return false;
    }

#undef BOOL_CASE
#undef BOOL_CASE_INNER
#undef SHIFT_CASE
#undef SHIFT_CASE_INNER
#undef CMP_BOOL_CASE
#undef BINOP_CASE

    return true;
}

static bool eval_ternary(
    lua_State *L, c_value &ret, c_expr const &e, c_expr_type &et
) WARN_UNUSED_RET;

static bool eval_ternary(
    lua_State *L, c_value &ret, c_expr const &e, c_expr_type &et
) {
    c_expr_type cet;
    c_value cval;
    if (!e.tern.cond->eval(L, cval, cet, false)) {
        return false;
    }
    bool tval = false;
    switch (cet) {
        case c_expr_type::INT: tval = cval.i; break;
        case c_expr_type::UINT: tval = cval.u; break;
        case c_expr_type::LONG: tval = cval.l; break;
        case c_expr_type::ULONG: tval = cval.ul; break;
        case c_expr_type::LLONG: tval = cval.ll; break;
        case c_expr_type::ULLONG: tval = cval.ull; break;
        case c_expr_type::FLOAT: tval = cval.f; break;
        case c_expr_type::DOUBLE: tval = cval.d; break;
        case c_expr_type::LDOUBLE: tval = cval.ld; break;
        case c_expr_type::STRING: tval = true; break;
        case c_expr_type::CHAR: tval = cval.c; break;
        case c_expr_type::NULLPTR: tval = false; break;
        case c_expr_type::BOOL: tval = cval.b; break;
        default:
            lua_pushliteral(L, "invalid ternary condition");
            return false;
    }
    if (tval) {
        return e.tern.texpr->eval(L, ret, et, true);
    }
    return e.tern.fexpr->eval(L, ret, et, true);
}

static bool c_expr_eval(
    lua_State *L, c_value &ret, c_expr const &ce, c_expr_type &et, bool promote
) WARN_UNUSED_RET;

static bool c_expr_eval(
    lua_State *L, c_value &ret, c_expr const &ce, c_expr_type &et, bool promote
) {
    switch (ce.type()) {
        case c_expr_type::BINARY:
            return eval_binary(L, ret, ce, et);
        case c_expr_type::UNARY:
            return eval_unary(L, ret, ce, et);
        case c_expr_type::TERNARY:
            return eval_ternary(L, ret, ce, et);
        case c_expr_type::INT:
            ret.i = ce.val.i; et = c_expr_type::INT; break;
        case c_expr_type::UINT:
            ret.u = ce.val.u; et = c_expr_type::UINT; break;
        case c_expr_type::LONG:
            ret.l = ce.val.l; et = c_expr_type::LONG; break;
        case c_expr_type::ULONG:
            ret.ul = ce.val.ul; et = c_expr_type::ULONG; break;
        case c_expr_type::LLONG:
            ret.ll = ce.val.ll; et = c_expr_type::LLONG; break;
        case c_expr_type::ULLONG:
            ret.ull = ce.val.ull; et = c_expr_type::ULLONG; break;
        case c_expr_type::FLOAT:
            ret.f = ce.val.f; et = c_expr_type::FLOAT; break;
        case c_expr_type::DOUBLE:
            ret.d = ce.val.d; et = c_expr_type::DOUBLE; break;
        case c_expr_type::CHAR:
            ret.c = ce.val.c; et = c_expr_type::CHAR; break;
        case c_expr_type::BOOL:
            ret.b = ce.val.b; et = c_expr_type::BOOL; break;
        default:
            ret.i = 0; et = c_expr_type::INVALID;
            lua_pushliteral(L, "invalid expression type");
            return false;
    }
    if (promote) {
        promote_int(ret, et);
    }
    return true;
}

bool c_expr::eval(
    lua_State *L, c_value &v, c_expr_type &et, bool promote
) const {
    /* clear first */
    v = c_value{};
    return c_expr_eval(L, v, *this, et, promote);
}

c_object::~c_object() {}

/* params ignore continuation func */
void c_param::do_serialize(util::strbuf &o, c_object_cont_f, void *) const {
    p_type.do_serialize(o, [](util::strbuf &out, void *data) {
        auto &p = *static_cast<c_param const *>(data);
        if (!p.p_name.empty()) {
            if (out.back() != '*') {
                out.append(' ');
            }
            out.append(p.p_name);
        }
    }, const_cast<c_param *>(this));
}

void c_function::do_serialize(
    util::strbuf &o, c_object_cont_f cont, void *data
) const {
    using D = struct {
        c_object_cont_f cont;
        void *data;
    };
    D val{cont, data};
    p_result.do_serialize(o, [](util::strbuf &out, void *idata) {
        D &d = *static_cast<D *>(idata);
        /* if cont is nullptr, we still need this for the final () anyway */
        if (
            (out.back() != '&') && (out.back() != '*') &&
            (out.back() != ']') && (out.back() != ')') &&
            (out.back() != '(')
        ) {
            out.append(' ');
        }
        if (d.cont) {
            out.append('(');
            auto sz = out.size();
            d.cont(out, d.data);
            if (sz == out.size()) {
                out.pop_back();
            } else {
                out.append(')');
            }
        }
    }, &val);
    o.append("()");
}

void c_type::clear() {
    int tp = type();
    if (tp == C_BUILTIN_FUNC) {
        using T = util::rc_obj<c_function>;
        p_func.~T();
    } else if ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_ARRAY)) {
        using T = util::rc_obj<c_type>;
        p_ptr.~T();
    }
}

void c_type::copy(c_type const &v) {
    p_asize = v.p_asize;
    p_ttype = v.p_ttype;
    p_flags = v.p_flags;
    p_cv = v.p_cv;

    int tp = type();
    if (tp == C_BUILTIN_FUNC) {
        new (&p_func) util::rc_obj<c_function>{v.p_func};
    } else if ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_ARRAY)) {
        new (&p_ptr) util::rc_obj<c_type>{v.p_ptr};
    } else if ((tp == C_BUILTIN_RECORD) || (tp == C_BUILTIN_ENUM)) {
        p_crec = v.p_crec;
    }
}

c_type::c_type(c_type &&v):
    p_asize{v.p_asize}, p_ttype{v.p_ttype}, p_flags{v.p_flags}, p_cv{v.p_cv}
{
    v.p_ttype = C_BUILTIN_INVALID;
    v.p_flags = 0;
    v.p_cv = 0;
    auto tp = type();
    if ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_ARRAY)) {
        using T = util::rc_obj<c_type>;
        new (&p_ptr) T{v.p_ptr};
        v.p_ptr.~T();
    } else if (tp == C_BUILTIN_FUNC) {
        using T = util::rc_obj<c_function>;
        new (&p_func) T{v.p_func};
        v.p_func.~T();
    } else {
        p_crec = v.p_crec;
        v.p_crec = nullptr;
    }
}

c_type &c_type::operator=(c_type &&v) {
    clear();
    p_asize = v.p_asize;
    p_ttype = v.p_ttype;
    p_flags = v.p_flags;
    p_cv = v.p_cv;
    v.p_ttype = C_BUILTIN_INVALID;
    v.p_flags = 0;
    v.p_cv = 0;
    auto tp = type();
    if ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_ARRAY)) {
        using T = util::rc_obj<c_type>;
        new (&p_ptr) T{v.p_ptr};
        v.p_ptr.~T();
    } else if (tp == C_BUILTIN_FUNC) {
        using T = util::rc_obj<c_function>;
        new (&p_func) T{v.p_func};
        v.p_func.~T();
    } else {
        p_crec = v.p_crec;
        v.p_crec = nullptr;
    }
    return *this;
}

static inline bool is_token(char c) {
    switch (c) {
        case '&':
        case '*':
        case '[':
        case ']':
        case '(':
        case ')':
            return true;
        default:
            break;
    }
    return false;
}

static inline void add_cv(util::strbuf &o, int cv, bool pre) {
    if (cv & C_CV_CONST) {
        if (!pre) {
            o.append(' ');
        }
        o.append("const");
        if (pre) {
            o.append(' ');
        }
    }
    if (cv & C_CV_VOLATILE) {
        if (!pre) {
            o.append(' ');
        }
        o.append("volatile");
        if (pre) {
            o.append(' ');
        }
    }
}

void c_type::do_serialize(
    util::strbuf &o, c_object_cont_f cont, void *data
) const {
    using D = struct {
        c_object_cont_f cont;
        void *data;
        c_type const *ct;
        int cv;
    };
    D val{cont, data, this, cv()};
    /* FIXME: don't use unref() */
    if (is_ref()) {
        unref().do_serialize(o, [](util::strbuf &out, void *idata) {
            D &d = *static_cast<D *>(idata);
            if (!is_token(out.back())) {
                out.append(' ');
            }
            out.append('&');
            if (d.cont) {
                d.cont(out, d.data);
            }
        }, &val);
        return;
    }
    switch (type()) {
        case C_BUILTIN_PTR:
            p_ptr->do_serialize(o, [](util::strbuf &out, void *idata) {
                D &d = *static_cast<D *>(idata);
                if (!is_token(out.back())) {
                    out.append(' ');
                }
                out.append('*');
                add_cv(out, d.cv, false);
                if (d.cont) {
                    d.cont(out, d.data);
                }
            }, &val);
            break;
        case C_BUILTIN_ARRAY:
            p_ptr->do_serialize(o, [](util::strbuf &out, void *idata) {
                D &d = *static_cast<D *>(idata);
                if (!is_token(out.back())) {
                    out.append(' ');
                }
                out.append('(');
                auto sz = out.size();
                if (d.cont) {
                    d.cont(out, d.data);
                }
                add_cv(out, d.ct->cv(), false);
                if (sz == out.size()) {
                    out.pop_back();
                    out.pop_back();
                } else if (out.back() != ']') {
                    out.append(')');
                } else {
                    out.remove(sz - 1);
                }
                out.append('[');
                if (d.ct->vla()) {
                    out.append('?');
                } else if (!d.ct->flex()) {
                    char buf[32];
                    util::write_u(buf, sizeof(buf), d.ct->array_size());
                    out.append(buf);
                }
                out.append(']');
            }, &val);
            break;
        case C_BUILTIN_FUNC:
            p_func->do_serialize(o, cont, data);
            return;
        case C_BUILTIN_RECORD:
            p_crec->do_serialize(o, cont, data);
            break;
        default:
            add_cv(o, val.cv, true);
            o.append(name());
            if (cont) {
                cont(o, data);
            }
            break;
    }
}

bool c_type::passable() const {
    switch (type()) {
        case C_BUILTIN_RECORD:
            return p_crec->passable();
        case C_BUILTIN_VOID:
        case C_BUILTIN_INVALID:
            return false;
        default:
            break;
    }
    return true;
}

#define C_BUILTIN_CASE(bt) case C_BUILTIN_##bt: \
    return ast::builtin_ffi_type<C_BUILTIN_##bt>();

ffi_type *c_type::libffi_type() const {
    if (is_ref()) {
        return &ffi_type_pointer;
    }

    switch (c_builtin(type())) {
        C_BUILTIN_CASE(VOID)
        C_BUILTIN_CASE(PTR)
        C_BUILTIN_CASE(ARRAY)
        C_BUILTIN_CASE(VA_LIST)

        case C_BUILTIN_FUNC:
            return p_func->libffi_type();

        case C_BUILTIN_RECORD:
            return p_crec->libffi_type();
        case C_BUILTIN_ENUM:
            return p_cenum->libffi_type();

        C_BUILTIN_CASE(FLOAT)
        C_BUILTIN_CASE(DOUBLE)
        C_BUILTIN_CASE(LDOUBLE)

        C_BUILTIN_CASE(BOOL)

        C_BUILTIN_CASE(CHAR)
        C_BUILTIN_CASE(SCHAR)
        C_BUILTIN_CASE(UCHAR)
        C_BUILTIN_CASE(SHORT)
        C_BUILTIN_CASE(USHORT)
        C_BUILTIN_CASE(INT)
        C_BUILTIN_CASE(UINT)
        C_BUILTIN_CASE(LONG)
        C_BUILTIN_CASE(ULONG)
        C_BUILTIN_CASE(LLONG)
        C_BUILTIN_CASE(ULLONG)

        case C_BUILTIN_INVALID:
            break;

        /* intentionally no default so that missing members are caught */
    }

    assert(false);
    return nullptr;
}

std::size_t c_type::alloc_size() const {
    switch (c_builtin(type())) {
        case C_BUILTIN_FUNC:
            return p_func->alloc_size();
        case C_BUILTIN_RECORD:
            return p_crec->alloc_size();
        case C_BUILTIN_ENUM:
            return p_cenum->alloc_size();
        case C_BUILTIN_ARRAY:
            /* may occasionally be zero sized, particularly where
             * dealt with entirely on the C side (so we don't know the
             * allocation size). That's fine, this is never relied upon
             * in contexts where that would be important
             */
            return p_asize * p_ptr->alloc_size();
        default:
            break;
    }
    return libffi_type()->size;
}

#undef C_BUILTIN_CASE

/* these sameness implementations are basic and non-compliant for now, just
 * to have something to get started with, edge cases will be covered later
 */

bool c_type::is_same(
    c_type const &other, bool ignore_cv, bool ignore_ref
) const {
    if (!ignore_cv && (cv() != other.cv())) {
        return false;
    }
    if (!ignore_ref && (is_ref() != other.is_ref())) {
        return false;
    }
    /* again manually covering all cases to make sure we really have them */
    switch (c_builtin(type())) {
        case C_BUILTIN_VOID:
        case C_BUILTIN_BOOL:
        case C_BUILTIN_VA_LIST:
        case C_BUILTIN_CHAR:
        case C_BUILTIN_SCHAR:
        case C_BUILTIN_UCHAR:
        case C_BUILTIN_SHORT:
        case C_BUILTIN_USHORT:
        case C_BUILTIN_INT:
        case C_BUILTIN_UINT:
        case C_BUILTIN_LONG:
        case C_BUILTIN_ULONG:
        case C_BUILTIN_LLONG:
        case C_BUILTIN_ULLONG:
        case C_BUILTIN_FLOAT:
        case C_BUILTIN_DOUBLE:
        case C_BUILTIN_LDOUBLE:
            return type() == other.type();

        case C_BUILTIN_FUNC:
            if (other.type() == C_BUILTIN_PTR) {
                if (other.ptr_base().type() == C_BUILTIN_FUNC) {
                    return is_same(other.ptr_base(), false, ignore_ref);
                }
                return false;
            } else if (other.type() == C_BUILTIN_FUNC) {
                return p_func->is_same(*other.p_func);
            }
            return false;

        case C_BUILTIN_ENUM:
            if (type() != other.type()) {
                return false;
            }
            return (p_cenum == other.p_cenum);

        case C_BUILTIN_RECORD:
            if (type() != other.type()) {
                return false;
            }
            return p_crec->is_same(*other.p_crec);

        case C_BUILTIN_PTR:
            if (other.type() == C_BUILTIN_FUNC) {
                if (ptr_base().type() == C_BUILTIN_FUNC) {
                    return ptr_base().is_same(other);
                }
                return false;
            }
            if (type() != other.type()) {
                return false;
            }
            return p_ptr->is_same(*other.p_ptr);

        case C_BUILTIN_ARRAY:
            if (type() != other.type()) {
                return false;
            }
            if (p_asize != other.p_asize) {
                return false;
            }
            return p_ptr->is_same(*other.p_ptr);

        case C_BUILTIN_INVALID:
            break;
    }

    assert(false);
    return false;
}

bool c_function::is_same(c_function const &other) const {
    if (!p_result.is_same(other.p_result)) {
        return false;
    }
    if (p_variadic != other.p_variadic) {
        return false;
    }
    if (p_params.size() != other.p_params.size()) {
        return false;
    }
    for (std::size_t i = 0; i < p_params.size(); ++i) {
        if (!p_params[i].type().is_same(other.p_params[i].type())) {
            return false;
        }
    }
    return true;
}

bool c_record::is_same(c_record const &other) const {
    return &other == this;
}

std::ptrdiff_t c_record::field_offset(
    char const *fname, c_type const *&fld
) const {
    std::ptrdiff_t ret = -1;
    iter_fields([fname, &ret, &fld](
        char const *ffname, ast::c_type const &ffld, std::size_t off
    ) {
        if (!std::strcmp(fname, ffname)) {
            ret = std::ptrdiff_t(off);
            fld = &ffld;
            return true;
        }
        return false;
    });
    return ret;
}

static inline ffi_type *libffi_base(ast::c_type const &tp, std::size_t &asz) {
    if (!tp.builtin_array()) {
        asz = 1;
        return tp.libffi_type();
    }
    auto sz = tp.array_size();
    auto *pb = &tp.ptr_base();
    while (pb->builtin_array()) {
        sz *= pb->array_size();
        pb = &pb->ptr_base();
    }
    asz = sz;
    return pb->libffi_type();
}

std::size_t c_record::iter_fields(bool (*cb)(
    char const *fname, ast::c_type const &type, std::size_t off, void *data
), void *data, std::size_t obase, bool &end) const {
    std::size_t base = 0;
    std::size_t nflds = p_fields.size();
    bool flex = false;
    bool uni = is_union();
    if (!uni && nflds && p_fields.back().type.flex()) {
         flex = true;
         --nflds;
    }
    for (std::size_t i = 0; i < nflds; ++i) {
        std::size_t asz;
        auto *tp = libffi_base(p_fields[i].type, asz);
        std::size_t align = tp->alignment;
        base = ((base + align - 1) / align) * align;
        if (p_fields[i].name.empty()) {
            /* transparent record is like a real member */
            assert(p_fields[i].type.type() == ast::C_BUILTIN_RECORD);
            p_fields[i].type.record().iter_fields(cb, data, base, end);
            if (end) {
                return base;
            }
        } else {
            end = cb(
                p_fields[i].name.data(), p_fields[i].type, obase + base, data
            );
            if (end) {
                return base;
            }
        }
        if (!uni) {
            base += tp->size * asz;
        }
    }
    if (flex) {
        base = p_ffi_type.size;
        end = cb(
            p_fields.back().name.data(), p_fields.back().type,
            obase + base, data
        );
    }
    return base;
}

#if FFI_CPU(ARM64) || defined(FFI_ARCH_PPC64_ELFV2)
#define FFI_UNION_HAGGREG 1
#endif

#ifdef FFI_UNION_HAGGREG
static ffi_type *union_base_type(c_type const &ct, std::size_t &asz) {
    ffi_type *ret = nullptr;

    if (ct.is_ref()) {
        return nullptr;
    }

    switch (ct.type()) {
        case C_BUILTIN_ARRAY:
            asz *= ct.array_size();
            return union_base_type(ct.ptr_base(), asz);

        case C_BUILTIN_FLOAT:
            return &ffi_type_float;
        case C_BUILTIN_DOUBLE:
            return &ffi_type_double;
        case C_BUILTIN_LDOUBLE:
            return &ffi_type_longdouble;

        case C_BUILTIN_RECORD:
            break;

        default:
            return nullptr;
    }

    auto &rec = ct.record();
    auto &flds = rec.raw_fields();

    for (std::size_t i = 0; i < flds.size(); ++i) {
        std::size_t nasz = 1;
        auto &fld = flds[i];
        ffi_type *hg = union_base_type(fld.type, nasz);
        if (!hg || (ret && (hg != ret))) {
            return nullptr;
        }
        asz += nasz;
        ret = hg;
    }
    return ret;
}
#endif

static ffi_type **resolve_union(
    util::vector<c_record::field> const &flds, ffi_type &fft
) {
    std::size_t nflds = flds.size();
    std::size_t usize = 0;
    unsigned short ualign = 0;
    bool maybe_homog = true;
    ffi_type *ubase = nullptr;

    for (std::size_t i = 0; i < nflds; ++i) {
        std::size_t asz = 1;
        ffi_type *try_ubase;
#ifdef FFI_UNION_HAGGREG
        try_ubase = union_base_type(flds[i].type, asz);
#else
        try_ubase = nullptr;
#endif
        if (!try_ubase) {
            maybe_homog = false;
            try_ubase = libffi_base(flds[i].type, asz);
        } else if (ubase && (try_ubase != ubase)) {
            maybe_homog = false;
        }
        if (try_ubase->alignment > ualign) {
            ualign = try_ubase->alignment;
        }
        if ((try_ubase->size * asz) > usize) {
            usize = try_ubase->size * asz;
        }
        if (maybe_homog) {
            ubase = try_ubase;
        }
    }

    fft.type = FFI_TYPE_STRUCT;
    /* the alignment is one thing we always know for sure */
    fft.alignment = ualign;

    if (maybe_homog) {
        /* homogenous aggregates: our size and alignment are specific and
         * strict, so just make up a structure that is N times our wanted
         * type where N is the number of times the type can fit in the size
         */
         std::size_t nelem = (usize / ubase->size);
         ffi_type **elems = new ffi_type *[nelem + 1];
         for (std::size_t i = 0; i < nelem; ++i) {
             elems[i] = ubase;
         }
         elems[nelem] = nullptr;
         fft.size = usize;
         fft.elements = &elems[0];
         return elems;
    }

    /* other types, fill with biggest integers that have an alignment
     * same or looser than the alignment of the biggest field, and size
     * same or smaller than the alignment to prevent overpadding
     */
    auto check_ubase = [ualign](auto &tp) {
        if (ualign % tp.alignment) {
            return false;
        }
        return (tp.alignment >= tp.size);
    };
    if (check_ubase(ffi_type_uint64)) {
        ubase = &ffi_type_uint64;
    } else if (check_ubase(ffi_type_uint32)) {
        ubase = &ffi_type_uint32;
    } else if (check_ubase(ffi_type_uint16)) {
        ubase = &ffi_type_uint16;
    } else {
        ubase = &ffi_type_uint8;
    }

    /* pad size to multiple of alignment: this is the real size of the type */
    usize = ((usize + ualign - 1) / ualign) * ualign;
    /* this is how many of selected base type can fit in wholly */
    std::size_t nelem = usize / ubase->size;
    /* pad the rest with bytes */
    std::size_t npad = usize - (nelem * ubase->size);
    /* the actual number of fields: whole elements + pad bytes + terminator */
    ffi_type **elems = new ffi_type *[nelem + npad + 1];
    for (std::size_t i = 0; i < nelem; ++i) {
        elems[i] = ubase;
    }
    for (std::size_t i = 0; i < npad; ++i) {
        elems[nelem + i] = &ffi_type_uchar;
    }
    elems[nelem] = nullptr;
    fft.size = usize;
    fft.elements = &elems[0];
    return elems;
}

void c_record::set_fields(util::vector<field> fields) {
    assert(p_fields.empty());
    assert(!p_elements);

    p_fields = util::move(fields);

    /* unions are handled specially; they are a struct that is filled
     * to the correct size and with correct types to satisfy ABI (when
     * passing is allowed); alignment is handled manually
     */
    if (is_union()) {
        p_elements = resolve_union(p_fields, p_ffi_type);
        return;
    }

    /* when dealing with flexible array members, we will need to pad the
     * struct to satisfy alignment of the flexible member, and use that
     * as the last member of the struct
     *
     * when the last member is a VLA, we don't know the size, so do the
     * same thing as when flexible, but make the VLA inaccessible
     */
    bool flex = !p_fields.empty() && p_fields.back().type.flex();
    std::size_t nfields = p_fields.size();
    std::size_t ffields = flex ? (nfields - 1) : nfields;

    std::size_t nelements = 0;
    for (std::size_t i = 0; i < ffields; ++i) {
        std::size_t asz;
        /* the type itself is unimportant for now but we need the count */
        libffi_base(p_fields[i].type, asz);
        nelements += asz;
    }

    p_elements = new ffi_type *[nelements + 1];

    p_ffi_type.size = p_ffi_type.alignment = 0;
    p_ffi_type.type = FFI_TYPE_STRUCT;

    p_ffi_type.elements = &p_elements[0];
    p_elements[nelements] = nullptr;

    for (std::size_t i = 0, e = 0; i < ffields; ++i) {
        std::size_t asz;
        auto *ft = libffi_base(p_fields[i].type, asz);
        for (std::size_t j = 0; j < asz; ++j) {
            p_elements[e + j] = ft;
        }
        e += asz;
    }

    if (flex) {
        /* for now null it, so ffi_prep_cif ignores it */
        p_elements[nelements] = nullptr;
    }

    /* fill in the size and alignment with an ugly hack
     *
     * we can make use of the size/alignment at runtime, so make sure
     * it's guaranteed to be properly filled in, even if the type has
     * not been used with a function
     *
     * for flexible array members the resulting size will need to get
     * padded a bit, do that afterwards
     */
    ffi_cif cif;
    /* this should generally not fail, as we're using the default ABI
     * and validating our type definitions beforehand, but maybe make
     * it a real error?
     */
    auto ret = ffi_prep_cif(
        &cif, FFI_DEFAULT_ABI, 0, &p_ffi_type, nullptr
    );
    static_cast<void>(ret); /* silence NDEBUG warnings */
    /* this should always succeed */
    assert(ret == FFI_OK);

    if (!flex) {
        return;
    }

    /* alignment of the base type of the final array */
    auto falign = p_fields.back().type.ptr_base().libffi_type()->alignment;
    auto padn = p_ffi_type.size % falign;

    /* the current size is an actual multiple, so no padding needed */
    if (!padn) {
        return;
    }
    /* otherwise create the padding struct */
    padn = falign - padn;
    p_felems = new ffi_type *[padn + 1];

    /* we know the size and alignment, since it's just padding bytes */
    p_ffi_flex.size = padn;
    p_ffi_flex.alignment = 1;
    for (std::size_t i = 0; i < padn; ++i) {
        p_felems[i] = &ffi_type_uchar;
    }
    p_felems[padn] = nullptr;
    p_ffi_flex.elements = &p_felems[0];

    /* and add it as a member + bump the size */
    p_elements[nelements] = &p_ffi_flex;
    p_ffi_type.size += padn;
}

/* decl store implementation, with overlaying for staging */

c_object const *decl_store::add(c_object *decl) {
    auto *oldecl = lookup(decl->name());
    if (oldecl) {
        auto ot = decl->obj_type();
        if (
            (ot != ast::c_object_type::VARIABLE) &&
            (ot != ast::c_object_type::TYPEDEF)
        ) {
            delete decl;
            return oldecl;
        } else {
            /* redefinitions of vars and funcs are okay
             * luajit doesn't check them so we don't either
             */
            delete decl;
            return nullptr;
        }
    }

    p_dlist.emplace_back(decl);
    auto &d = *p_dlist.back().value;
    p_dmap.insert(d.name(), &d);
    return nullptr;
}

void decl_store::commit() {
    /* this should only ever be used when staging */
    assert(p_base);
    /* reserve all space at once */
    p_base->p_dlist.reserve(p_base->p_dlist.size() + p_dlist.size());
    /* move all */
    for (std::size_t i = 0; i < p_dlist.size(); ++i) {
        p_base->p_dlist.push_back(util::move(p_dlist[i]));
    }
    /* set up mappings in base */
    p_dmap.for_each([this](char const *key, c_object *value) {
        p_base->p_dmap.insert(key, value);
    });
    p_base->name_counter += name_counter;
    drop();
}

void decl_store::drop() {
    p_dmap.clear();
    p_dlist.clear();
    name_counter = 0;
}

c_object const *decl_store::lookup(char const *name) const {
    auto *o = p_dmap.find(name);
    if (o) {
        return *o;
    }
    if (p_base) {
        return p_base->lookup(name);
    }
    return nullptr;
}

c_object *decl_store::lookup(char const *name) {
    auto *o = p_dmap.find(name);
    if (o) {
        return *o;
    }
    if (p_base) {
        return p_base->lookup(name);
    }
    return nullptr;
}

std::size_t decl_store::request_name(char *buf, std::size_t bufsize) {
    /* could do something better, this will do to avoid clashes for now... */
    std::size_t n = name_counter++;
    for (auto *pb = p_base; pb; pb = pb->p_base) {
        n += pb->name_counter;
    }
    return util::write_u(buf, bufsize, n);
}

c_type from_lua_type(lua_State *L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            return c_type{
                util::make_rc<c_type>(C_BUILTIN_VOID, 0),
                0, C_BUILTIN_PTR
            };
        case LUA_TBOOLEAN:
            return c_type{C_BUILTIN_BOOL, 0};
        case LUA_TNUMBER:
            static_assert(
                builtin_v<lua_Number> != C_BUILTIN_INVALID,
                "invalid lua_Number definition"
            );
            static_assert(
                builtin_v<lua_Integer> != C_BUILTIN_INVALID,
                "invalid lua_Integer definition"
            );
            /* 5.3+; always returns false on <= 5.2 */
            if (lua_isinteger(L, index)) {
                return c_type{builtin_v<lua_Integer>, 0};
            }
            return c_type{builtin_v<lua_Number>, 0};
        case LUA_TSTRING:
            return c_type{
                util::make_rc<c_type>(C_BUILTIN_CHAR, C_CV_CONST),
                0, C_BUILTIN_PTR
            };
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TTHREAD:
        case LUA_TLIGHTUSERDATA:
            /* by default use a void pointer, some will fail, that's ok */
            return c_type{
                util::make_rc<c_type>(C_BUILTIN_VOID, 0),
                0, C_BUILTIN_PTR
            };
        case LUA_TUSERDATA: {
            auto *cd = ffi::testcdata(L, index);
            if (!cd) {
                return c_type{
                    util::make_rc<c_type>(C_BUILTIN_VOID, 0),
                    0, C_BUILTIN_PTR
                };
            }
            return cd->decl.copy();
        }
        default:
            break;
    }
    assert(false);
    return c_type{C_BUILTIN_INVALID, 0};
}

} /* namespace ast */
