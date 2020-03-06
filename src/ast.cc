#include <cassert>
#include <limits>
#include <ctime>
#include <type_traits>

#include "ast.hh"
#include "ffi.hh"

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
    } else if ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_REF)) {
        delete p_ptr;
    }
}

c_type::c_type(c_type const &v): c_object{v.name}, p_type{v.p_type} {
    bool weak = !owns();
    int tp = type();
    if ((tp == C_BUILTIN_FPTR) || (tp == C_BUILTIN_FUNC)) {
        p_fptr = weak ? v.p_fptr : new c_function{*v.p_fptr};
    } else if ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_REF)) {
        p_ptr = weak ? v.p_ptr : new c_type{*v.p_ptr};
    } else if ((tp == C_BUILTIN_STRUCT) || (tp == C_BUILTIN_ENUM)) {
        p_ptr = v.p_ptr;
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
        case C_BUILTIN_REF:
            p_ptr->do_serialize(o);
            if (o.back() != '&') {
                o += ' ';
            }
            o += '&';
            break;
        case C_BUILTIN_FPTR:
        case C_BUILTIN_FUNC:
            /* cv is handled by func serializer */
            p_fptr->do_serialize_full(o, (ttp == C_BUILTIN_FPTR), tcv);
            return;
        case C_BUILTIN_STRUCT:
            p_crec->do_serialize(o);
            break;
        default:
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

#define C_BUILTIN_CASE(bt) case C_BUILTIN_##bt: \
    return ast::builtin_ffi_type<C_BUILTIN_##bt>();

ffi_type *c_type::libffi_type() const {
    switch (c_builtin(type())) {
        C_BUILTIN_CASE(VOID)
        C_BUILTIN_CASE(PTR)
        C_BUILTIN_CASE(REF)

        case C_BUILTIN_FPTR:
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
        case C_BUILTIN_FPTR:
        case C_BUILTIN_FUNC:
            return p_fptr->alloc_size();
        case C_BUILTIN_STRUCT:
            return p_crec->alloc_size();
        case C_BUILTIN_ENUM:
            return p_cenum->alloc_size();
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
        case C_BUILTIN_FPTR:
            if (
                /* FIXME: should not be equal but we still need
                 * to handle converting from func to funcptr
                 */
                (other.type() != C_BUILTIN_FUNC) &&
                (other.type() != C_BUILTIN_FPTR)
            ) {
                return false;
            }
            return p_cfptr->is_same(*other.p_cfptr);

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
            if (type() != other.type()) {
                return false;
            }
            return p_cptr->is_same(*other.p_cptr);

        case C_BUILTIN_REF:
            if (type() != other.type()) {
                return false;
            }
            return p_cptr->is_same(*other.p_cptr);

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

ptrdiff_t c_struct::field_offset(
    std::string const &fname, c_type const *&fld
) const {
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
    if (lookup(decl->name)) {
        delete decl;
        throw redefine_error{decl->name};
    }

    p_dlist.emplace_back(decl);
    auto &d = *p_dlist.back();
    p_dmap.emplace(d.name, &d);

    /* enums: register fields as constant values
     * FIXME: don't hardcode like this
     */
    if (d.obj_type() == c_object_type::ENUM) {
        for (auto &fld: d.as<c_enum>().fields()) {
            c_value val;
            val.i = fld.value;
            add(
                new c_constant{fld.name, c_type{"int", C_BUILTIN_INT, 0}, val}
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

c_object const *decl_store::lookup(std::string const &name) const {
    auto it = p_dmap.find(name);
    if (it != p_dmap.cend()) {
        return it->second;
    }
    if (p_base) {
        return p_base->lookup(name);
    }
    return nullptr;
}

c_object *decl_store::lookup(std::string const &name) {
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
            return c_type{c_type{"void", C_BUILTIN_VOID, 0}, 0};
        case LUA_TBOOLEAN:
            return c_type{"bool", C_BUILTIN_DOUBLE, 0};
        case LUA_TNUMBER:
            return c_type{"double", C_BUILTIN_DOUBLE, 0};
        case LUA_TSTRING:
            return c_type{c_type{"char", C_BUILTIN_CHAR, C_CV_CONST}, 0};
        case LUA_TTABLE:
        case LUA_TFUNCTION:
        case LUA_TTHREAD:
        case LUA_TLIGHTUSERDATA:
            /* by default use a void pointer, some will fail, that's ok */
            return c_type{c_type{"void", C_BUILTIN_VOID, 0}, 0};
        case LUA_TUSERDATA: {
            auto *cd = ffi::testcdata<ffi::noval>(L, index);
            if (!cd) {
                return c_type{c_type{"void", C_BUILTIN_VOID, 0}, 0};
            }
            return cd->decl;
        }
        default:
            break;
    }
    assert(false);
    return c_type{"", C_BUILTIN_INVALID, 0};
}

} /* namespace ast */
