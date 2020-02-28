#include <cassert>
#include <limits>
#include <ctime>
#include <type_traits>

#include "ast.hh"

namespace ast {

/* FIXME: implement actual integer promotions etc */

static c_value eval_unary(c_expr const &e) {
    c_value baseval = e.un.expr->eval();
    switch (e.un.op) {
        case c_expr_unop::UNP:
            break;
        case c_expr_unop::UNM:
            baseval.i = -baseval.i;
            break;
        case c_expr_unop::NOT:
            baseval.i = !baseval.i;
            break;
        case c_expr_unop::BNOT:
            baseval.i = ~baseval.i;
            break;
        default:
            assert(false);
            break;
    }
    return baseval;
}

static c_value eval_binary(c_expr const &e) {
    c_value lval = e.bin.lhs->eval();
    c_value rval = e.bin.rhs->eval();
    c_value ret;
    switch (e.bin.op) {
        case c_expr_binop::ADD:
            ret.i = lval.i + rval.i; break;
        case c_expr_binop::SUB:
            ret.i = lval.i - rval.i; break;
        case c_expr_binop::MUL:
            ret.i = lval.i * rval.i; break;
        case c_expr_binop::DIV:
            ret.i = lval.i / rval.i; break;
        case c_expr_binop::MOD:
            ret.i = lval.i % rval.i; break;

        case c_expr_binop::EQ:
            ret.i = (lval.i == rval.i); break;
        case c_expr_binop::NEQ:
            ret.i = (lval.i != rval.i); break;
        case c_expr_binop::GT:
            ret.i = (lval.i > rval.i); break;
        case c_expr_binop::LT:
            ret.i = (lval.i < rval.i); break;
        case c_expr_binop::GE:
            ret.i = (lval.i >= rval.i); break;
        case c_expr_binop::LE:
            ret.i = (lval.i <= rval.i); break;

        case c_expr_binop::AND:
            ret.i = lval.i && rval.i; break;
        case c_expr_binop::OR:
            ret.i = lval.i || rval.i; break;

        case c_expr_binop::BAND:
            ret.i = lval.i & rval.i; break;
        case c_expr_binop::BOR:
            ret.i = lval.i | rval.i; break;
        case c_expr_binop::BXOR:
            ret.i = lval.i ^ rval.i; break;
        case c_expr_binop::LSH:
            ret.i = lval.i << rval.i; break;
        case c_expr_binop::RSH:
            ret.i = lval.i >> rval.i; break;

        default:
            assert(false);
            break;
    }
    return ret;
}

static c_value eval_ternary(c_expr const &e) {
    c_value cval = e.tern.cond->eval();
    if (cval.i) {
        return e.tern.texpr->eval();
    }
    return e.tern.fexpr->eval();
}

c_value c_expr::eval() const {
    c_value ret;
    switch (type) {
        case c_expr_type::BINARY:
            return eval_binary(*this);
        case c_expr_type::UNARY:
            return eval_unary(*this);
        case c_expr_type::TERNARY:
            return eval_ternary(*this);
        case c_expr_type::INT:
            ret.i = val.i; break;
        case c_expr_type::UINT:
            ret.i = int(val.u); break;
        case c_expr_type::LONG:
            ret.i = int(val.l); break;
        case c_expr_type::ULONG:
            ret.i = int(val.ul); break;
        case c_expr_type::LLONG:
            ret.i = int(val.ll); break;
        case c_expr_type::ULLONG:
            ret.i = int(val.ull); break;
        case c_expr_type::FLOAT:
            ret.i = int(val.f); break;
        case c_expr_type::DOUBLE:
            ret.i = int(val.d); break;
        case c_expr_type::CHAR:
            ret.i = int(val.c); break;
        case c_expr_type::BOOL:
            ret.i = int(val.b); break;
        default:
            ret.i = 0; break;
    }
    return ret;
}

void c_param::do_serialize(std::string &o) const {
    p_type.do_serialize(o);
    if (!this->name.empty()) {
        if (o.back() != '*') {
            o += ' ';
        }
        o += this->name;
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

c_type::c_type(c_function tp, int qual, int cbt):
    c_object{}, p_fptr{new c_function{std::move(tp)}},
    p_type{cbt | uint32_t(qual)}
{}

c_type::~c_type() {
    if (!owns()) {
        return;
    }
    int tp = type();
    if ((tp == C_BUILTIN_FPTR) || (tp == C_BUILTIN_FUNC)) {
        delete p_fptr;
    } else if (tp == C_BUILTIN_PTR) {
        delete p_ptr;
    }
}

c_type::c_type(c_type const &v): c_object{v.name}, p_type{v.p_type} {
    bool weak = !owns();
    int tp = type();
    if ((tp == C_BUILTIN_FPTR) || (tp == C_BUILTIN_FUNC)) {
        p_fptr = weak ? v.p_fptr : new c_function{*v.p_fptr};
    } else if (tp == C_BUILTIN_PTR) {
        p_ptr = weak ? v.p_ptr : new c_type{*v.p_ptr};
    }
}

c_type::c_type(c_type &&v):
    c_object{std::move(v.name)}, p_ptr{std::exchange(v.p_ptr, nullptr)},
    p_type{v.p_type}
{}

void c_type::do_serialize(std::string &o) const {
    int tcv = cv();
    int ttp = type();
    switch (ttp) {
        case C_BUILTIN_PTR:
            p_ptr->do_serialize(o);
            if (o.back() != '*') {
                o += ' ';
            }
            o += '*';
            break;
        case C_BUILTIN_FPTR:
        case C_BUILTIN_FUNC:
            /* cv is handled by func serializer */
            p_fptr->do_serialize_full(o, (ttp == C_BUILTIN_FPTR), tcv);
            return;
        default:
            switch (type()) {
                case C_BUILTIN_CHAR:
                case C_BUILTIN_SHORT:
                case C_BUILTIN_LONG:
                case C_BUILTIN_LLONG:
                    if (tcv & C_CV_UNSIGNED) {
                        o += "unsigned ";
                    } else if (tcv & C_CV_SIGNED) {
                        o += "signed ";
                    }
                    break;
                default:
                    break;
            }
            o += this->name;
            break;
    }
    if (tcv & C_CV_CONST) {
        o += " const";
    }
    if (tcv & C_CV_VOLATILE) {
        o += " volatile";
    }
}

/*
    C_BUILTIN_INVALID = 0,

    C_BUILTIN_CHAR,
    C_BUILTIN_SHORT,
    C_BUILTIN_INT,
    C_BUILTIN_LONG,
    C_BUILTIN_LLONG,

    C_BUILTIN_INT8,
    C_BUILTIN_INT16,
    C_BUILTIN_INT32,
    C_BUILTIN_INT64,

    C_BUILTIN_SIZE,
    C_BUILTIN_INTPTR,
    C_BUILTIN_PTRDIFF,
*/

template<typename T>
constexpr ffi_type *get_basic_int() {
    bool is_signed = std::numeric_limits<T>::is_signed;
    switch (sizeof(T)) {
        case 8:
            return is_signed ? &ffi_type_sint64 : &ffi_type_uint64;
        case 4:
            return is_signed ? &ffi_type_sint32 : &ffi_type_uint32;
        case 2:
            return is_signed ? &ffi_type_sint16 : &ffi_type_uint16;
        case 1:
            return is_signed ? &ffi_type_sint8 : &ffi_type_uint8;
        default:
            break;
    }
    assert(false);
    return nullptr;
}

template<typename T>
constexpr ffi_type *get_basic_float() {
    if (std::is_same<T, float>::value) {
        return &ffi_type_float;
    } else if (std::is_same<T, double>::value) {
        return &ffi_type_double;
    } else if (std::is_same<T, long double>::value) {
        return &ffi_type_longdouble;
    } else {
        assert(false);
        return nullptr;
    }
}

/* more costly "runtime" version of the type detection */
template<typename T>
ffi_type *get_basic_intr(c_type const &tp) {
    int cv = tp.cv();

    /* default for the compile-time type */
    bool is_signed = std::numeric_limits<T>::is_signed;
    if (cv & ast::C_CV_SIGNED) {
        is_signed = true;
    } else if (cv & ast::C_CV_UNSIGNED) {
        is_signed = false;
    }

    if (is_signed) {
        return get_basic_int<typename std::make_signed<T>::type>();
    }
    return get_basic_int<typename std::make_unsigned<T>::type>();
}

ffi_type *c_type::libffi_type() const {
    switch (c_builtin(type())) {
        /* FIXME: remove this */
        case C_BUILTIN_NOT:
            return p_cptr->libffi_type();

        case C_BUILTIN_VOID:
            return &ffi_type_void;

        case C_BUILTIN_PTR:
            return &ffi_type_pointer;

        case C_BUILTIN_FPTR:
        case C_BUILTIN_FUNC:
            return p_fptr->libffi_type();

        case C_BUILTIN_STRUCT:
            return p_crec->libffi_type();
        case C_BUILTIN_ENUM:
            return p_cenum->libffi_type();

        case C_BUILTIN_FLOAT:
            return &ffi_type_float;
        case C_BUILTIN_DOUBLE:
            return &ffi_type_double;
        case C_BUILTIN_LDOUBLE:
            return &ffi_type_longdouble;

        case C_BUILTIN_BOOL:
            /* i guess... */
            return &ffi_type_uchar;

        case C_BUILTIN_CHAR:
            return get_basic_intr<char>(*this);
        case C_BUILTIN_SHORT:
            return get_basic_intr<short>(*this);
        case C_BUILTIN_INT:
            return get_basic_intr<int>(*this);
        case C_BUILTIN_LONG:
            return get_basic_intr<long>(*this);
        case C_BUILTIN_LLONG:
            return get_basic_intr<long long>(*this);
        case C_BUILTIN_INT8:
            return get_basic_intr<int8_t>(*this);
        case C_BUILTIN_INT16:
            return get_basic_intr<int16_t>(*this);
        case C_BUILTIN_INT32:
            return get_basic_intr<int32_t>(*this);
        case C_BUILTIN_INT64:
            return get_basic_intr<int64_t>(*this);
        case C_BUILTIN_SIZE:
            return get_basic_intr<size_t>(*this);
        case C_BUILTIN_INTPTR:
            return get_basic_intr<intptr_t>(*this);
        case C_BUILTIN_PTRDIFF:
            /* always signed */
            return get_basic_int<ptrdiff_t>();

        case C_BUILTIN_TIME:
            if (std::numeric_limits<time_t>::is_integer) {
                return get_basic_int<time_t>();
            } else {
                return get_basic_float<time_t>();
            }

        case C_BUILTIN_INVALID:
            printf("hi\n");
            break;

        /* intentionally no default so that missing members are caught */
    }

    printf("type %d\n", int(type()));

    assert(false);
    return nullptr;
}

/* lua is not thread safe, so the FFI doesn't need to be either */

/* the list of declarations; actually stored */
static std::vector<std::unique_ptr<c_object>> decl_list;

/* mapping for quick lookups */
static std::unordered_map<std::string, c_object const *> decl_map;

void add_decl(c_object *decl) {
    if (decl_map.find(decl->name) != decl_map.end()) {
        throw redefine_error{decl->name};
    }

    decl_list.emplace_back(decl);
    auto &d = *decl_list.back();
    decl_map.emplace(d.name, &d);

    /* enums: register fields as constant values
     * FIXME: don't hardcode like this
     */
    if (d.obj_type() == c_object_type::ENUM) {
        for (auto &fld: d.as<c_enum>().fields()) {
            c_value val;
            val.i = fld.value;
            add_decl(
                new c_constant{fld.name, c_type{"int", C_BUILTIN_INT, 0}, val}
            );
        }
    }
}

c_object const *lookup_decl(std::string const &name) {
    auto it = decl_map.find(name);
    if (it == decl_map.end()) {
        return nullptr;
    }
    return it->second;
}

} /* namespace ast */
