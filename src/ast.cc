#include <cassert>
#include <cstdint>
#include <limits>
#include <ctime>
#include <type_traits>

#include "platform.hh"
#include "ast.hh"
#include "ffi.hh"

namespace ast {

/* This unholy spaghetti implements integer promotions as well as conversion
 * rules in arithmetic operations etc. as the C standard defines it... it does
 * not handle errors yet (FIXME) and only covers types that can be emitted by
 * literals (i.e. no handling of names and so on).
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
static int get_rank(c_expr_type type) {
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
        default: assert(false); break;
    }
    return -1;
}

static bool is_signed(c_expr_type type) {
    switch (type) {
        case c_expr_type::CHAR:
            return std::numeric_limits<char>::is_signed;
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

static void convert_bin(
    c_value &lval, c_expr_type &let, c_value &rval, c_expr_type &ret
) {
    /* same types, likely case, bail out early */
    if (let == ret) {
        return;
    }

    int lrank = get_rank(let);
    int rrank = get_rank(ret);

    /* if right operand is higher ranked, treat it as left operand */
    if (rrank > lrank) {
        return convert_bin(rval, ret, lval, let);
    }

#define CONVERT_RVAL(lv) \
    switch (ret) { \
        case c_expr_type::DOUBLE: rval.lv = rval.d; break; \
        case c_expr_type::FLOAT: rval.lv = rval.f; break; \
        case c_expr_type::INT: rval.lv = rval.i; break; \
        case c_expr_type::UINT: rval.lv = rval.u; break; \
        case c_expr_type::LONG: rval.lv = rval.l; break; \
        case c_expr_type::ULONG: rval.lv = rval.ul; break; \
        case c_expr_type::LLONG: rval.lv = rval.ll; break; \
        case c_expr_type::ULLONG: rval.lv = rval.ull; break; \
        default: break;  \
    }

    /* at this point it's guaranteed that left rank is higer or equal */

    /* left operand is a float, convert to it */
    switch (let) {
        case c_expr_type::LDOUBLE:
            CONVERT_RVAL(ld); ret = let; return;
        case c_expr_type::DOUBLE:
            CONVERT_RVAL(d); ret = let; return;
        case c_expr_type::FLOAT:
            CONVERT_RVAL(f); ret = let; return;
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
                CONVERT_RVAL(ull); ret = let; return;
            case c_expr_type::LLONG:
                CONVERT_RVAL(ll); ret = let; return;
            case c_expr_type::ULONG:
                CONVERT_RVAL(ul); ret = let; return;
            case c_expr_type::LONG:
                CONVERT_RVAL(l); ret = let; return;
            case c_expr_type::UINT:
                CONVERT_RVAL(u); ret = let; return;
            case c_expr_type::INT:
                CONVERT_RVAL(i); ret = let; return;
            default:
                break;
        }
        /* should be unreachable */
        assert(false);
        return;
    }

    /* unsigned type has greater or equal rank */
    if (rsig) {
        switch (let) {
            case c_expr_type::ULLONG:
                CONVERT_RVAL(ull); ret = let; return;
            case c_expr_type::ULONG:
                CONVERT_RVAL(ul); ret = let; return;
            case c_expr_type::UINT:
                CONVERT_RVAL(u); ret = let; return;
            default:
                break;
        }
        /* should be unreachable */
        assert(false);
        return;
    }

#define CONVERT_RVAL_BOUNDED(lv) \
    switch (ret) { \
        case c_expr_type::ULONG: \
            if (sizeof(unsigned long) < sizeof(lval.lv)) { \
                rval.lv = rval.ul; \
                ret = let; \
                return; \
            } \
            break; \
        case c_expr_type::UINT: \
            if (sizeof(unsigned int) < sizeof(lval.lv)) { \
                rval.lv = rval.u; \
                ret = let; \
                return; \
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
            return;
        case c_expr_type::LONG:
            lval.ul = lval.l;
            CONVERT_RVAL(ul);
            let = ret = c_expr_type::ULONG;
            return;
        case c_expr_type::INT:
            lval.u = lval.i;
            CONVERT_RVAL(u);
            let = ret = c_expr_type::UINT;
            return;
        default:
            break;
    }

#undef CONVERT_RVAL

    /* unreachable */
    assert(false);
}

static c_value eval_unary(c_expr const &e, c_expr_type &et) {
    c_value baseval = e.un.expr->eval(et);
    switch (e.un.op) {
        case c_expr_unop::UNP:
            promote_int(baseval, et);
            break;
        case c_expr_unop::UNM:
            promote_int(baseval, et);
            switch (et) {
                case c_expr_type::INT: baseval.i = -baseval.i; break;
                case c_expr_type::UINT: baseval.u = -baseval.u; break;
                case c_expr_type::LONG: baseval.l = -baseval.l; break;
                case c_expr_type::ULONG: baseval.ul = -baseval.ul; break;
                case c_expr_type::LLONG: baseval.ll = -baseval.ll; break;
                case c_expr_type::ULLONG: baseval.ull = -baseval.ull; break;
                default: break;
            }
            break;
        case c_expr_unop::NOT:
            switch (et) {
                case c_expr_type::BOOL: baseval.b = !baseval.b; break;
                case c_expr_type::CHAR: baseval.c = !baseval.c; break;
                case c_expr_type::INT: baseval.i = !baseval.i; break;
                case c_expr_type::UINT: baseval.u = !baseval.u; break;
                case c_expr_type::LONG: baseval.l = -baseval.l; break;
                case c_expr_type::ULONG: baseval.ul = -baseval.ul; break;
                case c_expr_type::LLONG: baseval.ll = -baseval.ll; break;
                case c_expr_type::ULLONG: baseval.ull = -baseval.ull; break;
                default: break;
            }
            break;
        case c_expr_unop::BNOT:
            promote_int(baseval, et);
            switch (et) {
                case c_expr_type::INT: baseval.i = ~baseval.i; break;
                case c_expr_type::UINT: baseval.u = ~baseval.u; break;
                case c_expr_type::LONG: baseval.l = ~baseval.l; break;
                case c_expr_type::ULONG: baseval.ul = ~baseval.ul; break;
                case c_expr_type::LLONG: baseval.ll = ~baseval.ll; break;
                case c_expr_type::ULLONG: baseval.ull = ~baseval.ull; break;
                default: break;
            }
            break;
        default:
            assert(false);
            break;
    }
    return baseval;
}

static c_value eval_binary(c_expr const &e, c_expr_type &et) {
    c_expr_type let, ret;
    c_value lval = e.bin.lhs->eval(let);
    c_value rval = e.bin.rhs->eval(ret);
    c_value retv;

#define BINOP_CASE(opn, op) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        convert_bin(lval, let, rval, ret); \
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
            default: assert(false); break; \
        } \
        break;

#define BINOP_CASE_NOFLT(opn, op) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        convert_bin(lval, let, rval, ret); \
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
                assert(false); \
                break; \
            default: assert(false); break; \
        } \
        break;

#define SHIFT_CASE_INNER(fn, op) \
    switch (ret) { \
        case c_expr_type::INT: retv.fn = lval.fn op rval.i; break; \
        case c_expr_type::UINT: retv.fn = lval.fn op rval.u; break; \
        case c_expr_type::LONG: retv.fn = lval.fn op rval.l; break; \
        case c_expr_type::ULONG: retv.fn = lval.fn op rval.u; break; \
        case c_expr_type::LLONG: retv.fn = lval.fn op rval.ul; break; \
        case c_expr_type::ULLONG: retv.fn = lval.fn op rval.ull; break; \
        default: assert(false); break; \
    }

#define SHIFT_CASE(opn, op) \
    case c_expr_binop::opn: \
        promote_int(lval, let); \
        promote_int(rval, ret); \
        switch (let) { \
            case c_expr_type::INT: SHIFT_CASE_INNER(i, op); break; \
            case c_expr_type::UINT: SHIFT_CASE_INNER(u, op); break; \
            case c_expr_type::LONG: SHIFT_CASE_INNER(l, op); break; \
            case c_expr_type::ULONG: SHIFT_CASE_INNER(ul, op); break; \
            case c_expr_type::LLONG: SHIFT_CASE_INNER(ll, op); break; \
            case c_expr_type::ULLONG: SHIFT_CASE_INNER(ull, op); break; \
            default: assert(false); break; \
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
        default: assert(false); break; \
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
            default: assert(false); break; \
        } \
        break;

    switch (e.bin.op) {
        BINOP_CASE(ADD, +)
        BINOP_CASE(SUB, -)
        BINOP_CASE(MUL, *)
        BINOP_CASE(DIV, /)
        BINOP_CASE_NOFLT(MOD, %)

        BINOP_CASE(EQ, ==)
        BINOP_CASE(NEQ, !=)
        BINOP_CASE(GT, >)
        BINOP_CASE(LT, <)
        BINOP_CASE(GE, >=)
        BINOP_CASE(LE, <=)

        BOOL_CASE(AND, &&)
        BOOL_CASE(OR, ||)

        BINOP_CASE_NOFLT(BAND, &)
        BINOP_CASE_NOFLT(BOR, |)
        BINOP_CASE_NOFLT(BXOR, ^)
        SHIFT_CASE(LSH, <<)
        SHIFT_CASE(RSH, >>)

        default:
            assert(false);
            break;
    }

#undef BOOL_CASE
#undef BOOL_CASE_INNER
#undef SHIFT_CASE
#undef SHIFT_CASE_INNER
#undef BINOP_CASE

    return retv;
}

static c_value eval_ternary(c_expr const &e, c_expr_type &et) {
    c_expr_type cet;
    c_value cval = e.tern.cond->eval(cet);
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
            assert(false);
            break;
    }
    if (tval) {
        return e.tern.texpr->eval(et, true);
    }
    return e.tern.fexpr->eval(et, true);
}

c_value c_expr::eval(c_expr_type &et, bool promote) const {
    c_value ret;
    switch (type) {
        case c_expr_type::BINARY:
            return eval_binary(*this, et);
        case c_expr_type::UNARY:
            return eval_unary(*this, et);
        case c_expr_type::TERNARY:
            return eval_ternary(*this, et);
        case c_expr_type::INT:
            ret.i = val.i; et = type; break;
        case c_expr_type::UINT:
            ret.u = val.u; et = type; break;
        case c_expr_type::LONG:
            ret.l = val.l; et = type; break;
        case c_expr_type::ULONG:
            ret.ul = val.ul; et = type; break;
        case c_expr_type::LLONG:
            ret.ll = val.ll; et = type; break;
        case c_expr_type::ULLONG:
            ret.ull = val.ull; et = type; break;
        case c_expr_type::FLOAT:
            ret.f = val.f; et = type; break;
        case c_expr_type::DOUBLE:
            ret.d = val.d; et = type; break;
        case c_expr_type::CHAR:
            ret.c = val.c; et = type; break;
        case c_expr_type::BOOL:
            ret.b = val.b; et = type; break;
        default:
            ret.i = 0; et = c_expr_type::INVALID; break;
    }
    if (promote) {
        promote_int(ret, et);
    }
    return ret;
}

void c_param::do_serialize(std::string &o) const {
    p_type.do_serialize(o);
    if (!this->p_name.empty()) {
        if (o.back() != '*') {
            o += ' ';
        }
        o += this->p_name;
    }
}

void c_function::do_serialize_full(std::string &o, bool fptr, int cv) const {
    p_result.do_serialize(o);
    if (o.back() != '*') {
        o += ' ';
    }
    if (!fptr) {
        o += "()";
        return;
    }
    o += "(*";
    if (cv & C_CV_CONST) {
        o += " const";
    }
    if (cv & C_CV_VOLATILE) {
        o += " volatile";
    }
    o += ")()";
}

c_type::~c_type() {
    if (!owns()) {
        return;
    }
    int tp = type();
    if (tp == C_BUILTIN_FUNC) {
        delete p_fptr;
    } else if (
        (tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_REF) ||
        (tp == C_BUILTIN_ARRAY)
    ) {
        delete p_ptr;
    }
}

c_type::c_type(c_type const &v): p_type{v.p_type} {
    bool weak = !owns();
    int tp = type();
    if (tp == C_BUILTIN_FUNC) {
        p_fptr = weak ? v.p_fptr : new c_function{*v.p_fptr};
    } else if (
        (tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_REF) ||
        (tp == C_BUILTIN_ARRAY)
    ) {
        p_ptr = weak ? v.p_ptr : new c_type{*v.p_ptr};
    } else if ((tp == C_BUILTIN_STRUCT) || (tp == C_BUILTIN_ENUM)) {
        p_ptr = v.p_ptr;
    }
}

c_type::c_type(c_type &&v):
    p_ptr{std::exchange(v.p_ptr, nullptr)},
    p_type{v.p_type}
{}

/* FIXME: a bunch of these are wrong */
void c_type::do_serialize(std::string &o) const {
    int tcv = cv();
    int ttp = type();
    switch (ttp) {
        case C_BUILTIN_PTR:
            if (p_ptr->type() == C_BUILTIN_FUNC) {
                p_ptr->function().do_serialize_full(o, true, tcv);
                break;
            }
            p_ptr->do_serialize(o);
            if (o.back() != '*') {
                o += ' ';
            }
            o += '*';
            break;
        case C_BUILTIN_REF:
            p_ptr->do_serialize(o);
            if ((o.back() != '&') && (o.back() != '*')) {
                o += ' ';
            }
            o += '&';
            break;
        case C_BUILTIN_ARRAY:
            p_ptr->do_serialize(o);
            if ((o.back() != '&') && (o.back() != '*') && (o.back() != ']')) {
                o += ' ';
            }
            o += '[';
            if (!vla()) {
                char buf[32];
                snprintf(buf, sizeof(buf), "%zu", array_size());
                o += static_cast<char const *>(buf);
            } else {
                o += '?';
            }
            o += ']';
            break;
        case C_BUILTIN_FUNC:
            /* cv is handled by func serializer */
            p_fptr->do_serialize_full(o, false, tcv);
            return;
        case C_BUILTIN_STRUCT:
            p_crec->do_serialize(o);
            break;
        default:
            o += this->name();
            break;
    }
    if (tcv & C_CV_CONST) {
        o += " const";
    }
    if (tcv & C_CV_VOLATILE) {
        o += " volatile";
    }
}

#define C_BUILTIN_CASE(bt) case C_BUILTIN_##bt: \
    return ast::builtin_ffi_type<C_BUILTIN_##bt>();

ffi_type *c_type::libffi_type() const {
    switch (c_builtin(type())) {
        C_BUILTIN_CASE(VOID)
        C_BUILTIN_CASE(PTR)
        C_BUILTIN_CASE(REF)
        C_BUILTIN_CASE(ARRAY)
        C_BUILTIN_CASE(VA_LIST)

        case C_BUILTIN_FUNC:
            return p_fptr->libffi_type();

        case C_BUILTIN_STRUCT:
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

        C_BUILTIN_CASE(WCHAR)
        C_BUILTIN_CASE(CHAR16)
        C_BUILTIN_CASE(CHAR32)

        C_BUILTIN_CASE(INT8)
        C_BUILTIN_CASE(INT16)
        C_BUILTIN_CASE(INT32)
        C_BUILTIN_CASE(INT64)
        C_BUILTIN_CASE(UINT8)
        C_BUILTIN_CASE(UINT16)
        C_BUILTIN_CASE(UINT32)
        C_BUILTIN_CASE(UINT64)

        C_BUILTIN_CASE(SIZE)
        C_BUILTIN_CASE(SSIZE)
        C_BUILTIN_CASE(INTPTR)
        C_BUILTIN_CASE(UINTPTR)
        C_BUILTIN_CASE(PTRDIFF)

        C_BUILTIN_CASE(TIME)

        case C_BUILTIN_INVALID:
            break;

        /* intentionally no default so that missing members are caught */
    }

    assert(false);
    return nullptr;
}

size_t c_type::alloc_size() const {
    switch (c_builtin(type())) {
        case C_BUILTIN_FUNC:
            return p_fptr->alloc_size();
        case C_BUILTIN_STRUCT:
            return p_crec->alloc_size();
        case C_BUILTIN_ENUM:
            return p_cenum->alloc_size();
        case C_BUILTIN_ARRAY:
            /* VLAs may occasionally be zero sized, particularly where
             * dealt with entirely on the C side (so we don't know the
             * allocation size). That's fine, this is never relied upon
             * in contexts where that would be important
             */
            return p_asize * p_cptr->alloc_size();
        default:
            break;
    }
    return libffi_type()->size;
}

#undef C_BUILTIN_CASE

/* these sameness implementations are basic and non-compliant for now, just
 * to have something to get started with, edge cases will be covered later
 */

bool c_type::is_same(c_type const &other, bool ignore_cv) const {
    if (!ignore_cv && (cv() != other.cv())) {
        return false;
    }
    /* again manually covering all cases to make sure we really have them */
    switch (c_builtin(type())) {
        case C_BUILTIN_VOID:
        case C_BUILTIN_BOOL:
            /* simple identity */
            return type() == other.type();

        case C_BUILTIN_FUNC:
            if (other.type() == C_BUILTIN_PTR) {
                if (other.ptr_base().type() == C_BUILTIN_FUNC) {
                    return is_same(other.ptr_base());
                }
                return false;
            } else if (other.type() == C_BUILTIN_FUNC) {
                return p_cfptr->is_same(*other.p_cfptr);
            }
            return false;

        case C_BUILTIN_ENUM:
            if (type() != other.type()) {
                return false;
            }
            return (p_cenum == other.p_cenum);

        case C_BUILTIN_STRUCT:
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
            return p_cptr->is_same(*other.p_cptr);

        case C_BUILTIN_REF:
            if (type() != other.type()) {
                return false;
            }
            return p_cptr->is_same(*other.p_cptr);

        case C_BUILTIN_ARRAY:
            if (type() != other.type()) {
                return false;
            }
            if (p_asize != other.p_asize) {
                return false;
            }
            return p_cptr->is_same(*other.p_cptr);

        case C_BUILTIN_VA_LIST:
            return type() == other.type();

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
        case C_BUILTIN_WCHAR:
        case C_BUILTIN_CHAR16:
        case C_BUILTIN_CHAR32:
        case C_BUILTIN_INT8:
        case C_BUILTIN_INT16:
        case C_BUILTIN_INT32:
        case C_BUILTIN_INT64:
        case C_BUILTIN_UINT8:
        case C_BUILTIN_UINT16:
        case C_BUILTIN_UINT32:
        case C_BUILTIN_UINT64:
        case C_BUILTIN_SIZE:
        case C_BUILTIN_SSIZE:
        case C_BUILTIN_INTPTR:
        case C_BUILTIN_UINTPTR:
        case C_BUILTIN_PTRDIFF:
        case C_BUILTIN_TIME:
        case C_BUILTIN_FLOAT:
        case C_BUILTIN_DOUBLE:
        case C_BUILTIN_LDOUBLE:
            /* basic scalars use builtin libffi types */
            return libffi_type() == other.libffi_type();

        case C_BUILTIN_INVALID:
            break;
    }

    assert(false);
    return false;
}

static bool type_converts_to(c_type const &a, c_type const &b, bool ref) {
    if (ref) {
        /* if the new type has weaker cv, don't convert */
        if ((a.cv() & C_CV_CONST) && !(b.cv() & C_CV_CONST)) {
            return false;
        }
        if ((a.cv() & C_CV_VOLATILE) && !(b.cv() & C_CV_VOLATILE)) {
            return false;
        }
    }
    if (a.type() == C_BUILTIN_PTR) {
        if ((b.type() != C_BUILTIN_PTR) && (b.type() != C_BUILTIN_REF)) {
            return false;
        }
        return type_converts_to(a.ptr_base(), b.ptr_base(), true);
    } else if (a.type() == C_BUILTIN_REF) {
        if (b.type() == C_BUILTIN_REF) {
            return type_converts_to(a.ptr_base(), b.ptr_base(), false);
        } else if (b.type() == C_BUILTIN_PTR) {
            return type_converts_to(a.ptr_base(), b.ptr_base(), true);
        }
        return type_converts_to(a.ptr_base(), b, false);
    } else if (b.type() == C_BUILTIN_REF) {
        return type_converts_to(a, b.ptr_base(), false);
    }
    /* converting between voidptrs is ok in C always */
    if (ref && ((a.type() == C_BUILTIN_VOID) || (b.type() == C_BUILTIN_VOID))) {
        return true;
    }
    return a.is_same(b);
}

bool c_type::converts_to(c_type const &other) const {
    return type_converts_to(*this, other, false);
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
    for (size_t i = 0; i < p_params.size(); ++i) {
        if (!p_params[i].type().is_same(other.p_params[i].type())) {
            return false;
        }
    }
    return true;
}

bool c_struct::is_same(c_struct const &other) const {
    if (p_ffi_type.size != other.p_ffi_type.size) {
        return false;
    }
    if (p_ffi_type.alignment != other.p_ffi_type.alignment) {
        return false;
    }
    if (p_fields.size() != other.p_fields.size()) {
        return false;
    }
    for (size_t i = 0; i < p_fields.size(); ++i) {
        if (!p_fields[i].type.is_same(other.p_fields[i].type)) {
            return false;
        }
    }
    return true;
}

ptrdiff_t c_struct::field_offset(char const *fname, c_type const *&fld) const {
    ptrdiff_t base = 0;
    for (size_t i = 0; i < p_fields.size(); ++i) {
        auto *tp = p_elements[i];
        size_t align = tp->alignment;
        base = ((base + align - 1) / align) * align;
        if (p_fields[i].name.empty()) {
            /* transparent struct is like a real struct member */
            assert(p_fields[i].type.type() == ast::C_BUILTIN_STRUCT);
            auto moff = p_fields[i].type.record().field_offset(fname, fld);
            if (moff >= 0) {
                return base + moff;
            }
        } else if (p_fields[i].name == fname) {
            fld = &p_fields[i].type;
            return base;
        }
        base += tp->size;
    }
    fld = nullptr;
    return -1;
}

void c_struct::set_fields(std::vector<field> fields) {
    assert(p_fields.empty());
    assert(!p_elements);

    p_fields = std::move(fields);

    p_elements = std::unique_ptr<ffi_type *[]>{
        new ffi_type *[p_fields.size() + 1]
    };

    p_ffi_type.size = p_ffi_type.alignment = 0;
    p_ffi_type.type = FFI_TYPE_STRUCT;

    for (size_t i = 0; i < p_fields.size(); ++i) {
        p_elements[i] = p_fields[i].type.libffi_type();
    }
    p_elements[p_fields.size()] = nullptr;

    p_ffi_type.elements = &p_elements[0];

    /* fill in the size and alignment with an ugly hack
     *
     * we can make use of the size/alignment at runtime, so make sure
     * it's guaranteed to be properly filled in, even if the type has
     * not been used with a function
     */
    ffi_cif cif;
    /* this should generally not fail, as we're using the default ABI
     * and validating our type definitions beforehand, but maybe make
     * it a real error?
     */
    assert(ffi_prep_cif(
        &cif, FFI_DEFAULT_ABI, 0, &p_ffi_type, nullptr
    ) == FFI_OK);
}

/* decl store implementation, with overlaying for staging */

void decl_store::add(c_object *decl) {
    if (lookup(decl->name())) {
        redefine_error rd{decl->name()};
        delete decl;
        throw rd;
    }

    p_dlist.emplace_back(decl);
    auto &d = *p_dlist.back();
    p_dmap.emplace(d.name(), &d);

    /* enums: register fields as constant values
     * FIXME: don't hardcode like this
     */
    if (d.obj_type() == c_object_type::ENUM) {
        for (auto &fld: d.as<c_enum>().fields()) {
            c_value val;
            val.i = fld.value;
            add(
                new c_constant{fld.name, c_type{C_BUILTIN_INT, 0}, val}
            );
        }
    }
}

void decl_store::commit() {
    /* this should only ever be used when staging */
    assert(p_base);
    /* reserve all space at once */
    p_base->p_dlist.reserve(p_base->p_dlist.size() + p_dlist.size());
    /* move all */
    for (auto &u: p_dlist) {
        p_base->p_dlist.push_back(std::move(u));
    }
    /* set up mappings in base */
    for (auto const &p: p_dmap) {
        p_base->p_dmap.emplace(p);
    }
    drop();
}

void decl_store::drop() {
    p_dmap.clear();
    p_dlist.clear();
}

c_object const *decl_store::lookup(char const *name) const {
    auto it = p_dmap.find(name);
    if (it != p_dmap.cend()) {
        return it->second;
    }
    if (p_base) {
        return p_base->lookup(name);
    }
    return nullptr;
}

c_object *decl_store::lookup(char const *name) {
    auto it = p_dmap.find(name);
    if (it != p_dmap.end()) {
        return it->second;
    }
    if (p_base) {
        return p_base->lookup(name);
    }
    return nullptr;
}

std::string decl_store::request_name() const {
    char buf[32];
    /* could do something better, this will do to avoid clashes for now... */
    size_t n = 0;
    decl_store const *pb = this;
    do {
        n += pb->p_dlist.size();
        pb = pb->p_base;
    } while (pb);
    snprintf(buf, sizeof(buf), "%zu", n);
    return std::string{static_cast<char const *>(buf)};
}

c_type from_lua_type(lua_State *L, int index) {
    switch (lua_type(L, index)) {
        case LUA_TNIL:
            return c_type{c_type{C_BUILTIN_VOID, 0}, 0};
        case LUA_TBOOLEAN:
            return c_type{C_BUILTIN_BOOL, 0};
        case LUA_TNUMBER:
            static_assert(
                builtin_v<lua_Number> != C_BUILTIN_INVALID,
                "invalid lua_Number definition"
            );
            return c_type{builtin_v<lua_Number>, 0};
        case LUA_TSTRING:
            return c_type{c_type{C_BUILTIN_CHAR, C_CV_CONST}, 0};
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TTHREAD:
        case LUA_TLIGHTUSERDATA:
            /* by default use a void pointer, some will fail, that's ok */
            return c_type{c_type{C_BUILTIN_VOID, 0}, 0};
        case LUA_TUSERDATA: {
            auto *cd = ffi::testcdata<ffi::noval>(L, index);
            if (!cd) {
                return c_type{c_type{C_BUILTIN_VOID, 0}, 0};
            }
            return cd->decl;
        }
        default:
            break;
    }
    assert(false);
    return c_type{C_BUILTIN_INVALID, 0};
}

} /* namespace ast */
