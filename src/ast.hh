#ifndef AST_HH
#define AST_HH

#include <cstring>
#include <cstddef>
#include <cassert>
#include <ctime>

#include "lua.hh"
#include "libffi.hh"

#include <type_traits>
#include <limits>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <stdexcept>

namespace ast {

enum c_builtin {
    C_BUILTIN_INVALID = 0,

    C_BUILTIN_VOID,

    C_BUILTIN_REF,
    C_BUILTIN_PTR,

    C_BUILTIN_FUNC,
    C_BUILTIN_RECORD,
    C_BUILTIN_ARRAY,

    C_BUILTIN_VA_LIST,

    /* everything past this matches type.arith() */

    C_BUILTIN_ENUM,

    C_BUILTIN_BOOL,

    C_BUILTIN_CHAR,
    C_BUILTIN_SCHAR,
    C_BUILTIN_UCHAR,
    C_BUILTIN_SHORT,
    C_BUILTIN_USHORT,
    C_BUILTIN_INT,
    C_BUILTIN_UINT,
    C_BUILTIN_LONG,
    C_BUILTIN_ULONG,
    C_BUILTIN_LLONG,
    C_BUILTIN_ULLONG,

    C_BUILTIN_FLOAT,
    C_BUILTIN_DOUBLE,
    C_BUILTIN_LDOUBLE,
};

namespace detail {
    template<typename T>
    struct builtin_traits_base {
        using type = T;
        static ffi_type *libffi_type() { return ffi::ffi_traits<T>::type(); }
    };
} /* namespace detail */

/* only defined for arithmetic types with direct mappings */
template<c_builtin> struct builtin_traits;

template<> struct builtin_traits<C_BUILTIN_VOID>:
    detail::builtin_traits_base<void> {};

template<> struct builtin_traits<C_BUILTIN_PTR>:
    detail::builtin_traits_base<void *> {};

template<> struct builtin_traits<C_BUILTIN_REF>:
    detail::builtin_traits_base<char &> {};

template<> struct builtin_traits<C_BUILTIN_ARRAY>:
    detail::builtin_traits_base<char[]> {};

template<> struct builtin_traits<C_BUILTIN_VA_LIST> {
    using type = va_list;

    /* special case; XXX is this good enough ABI-wise? */
    static ffi_type *libffi_type() {
        return &ffi_type_pointer;
    }
};

template<> struct builtin_traits<C_BUILTIN_CHAR>:
    detail::builtin_traits_base<char> {};

template<> struct builtin_traits<C_BUILTIN_SCHAR>:
    detail::builtin_traits_base<signed char> {};

template<> struct builtin_traits<C_BUILTIN_UCHAR>:
    detail::builtin_traits_base<unsigned char> {};

template<> struct builtin_traits<C_BUILTIN_SHORT>:
    detail::builtin_traits_base<short> {};

template<> struct builtin_traits<C_BUILTIN_USHORT>:
    detail::builtin_traits_base<unsigned short> {};

template<> struct builtin_traits<C_BUILTIN_INT>:
    detail::builtin_traits_base<int> {};

template<> struct builtin_traits<C_BUILTIN_UINT>:
    detail::builtin_traits_base<unsigned int> {};

template<> struct builtin_traits<C_BUILTIN_LONG>:
    detail::builtin_traits_base<long> {};

template<> struct builtin_traits<C_BUILTIN_ULONG>:
    detail::builtin_traits_base<unsigned long> {};

template<> struct builtin_traits<C_BUILTIN_LLONG>:
    detail::builtin_traits_base<long long> {};

template<> struct builtin_traits<C_BUILTIN_ULLONG>:
    detail::builtin_traits_base<unsigned long long> {};

template<> struct builtin_traits<C_BUILTIN_FLOAT>:
    detail::builtin_traits_base<float> {};

template<> struct builtin_traits<C_BUILTIN_DOUBLE>:
    detail::builtin_traits_base<double> {};

template<> struct builtin_traits<C_BUILTIN_LDOUBLE>:
    detail::builtin_traits_base<long double> {};

template<> struct builtin_traits<C_BUILTIN_BOOL>:
    detail::builtin_traits_base<bool> {};

template<c_builtin t> using builtin_t = typename builtin_traits<t>::type;

namespace detail {
    template<typename T> struct builtin_v_base {
        static constexpr c_builtin value = C_BUILTIN_INVALID;
    };
    template<> struct builtin_v_base<void> {
        static constexpr c_builtin value = C_BUILTIN_VOID;
    };
    template<> struct builtin_v_base<bool> {
        static constexpr c_builtin value = C_BUILTIN_BOOL;
    };
    template<> struct builtin_v_base<char> {
        static constexpr c_builtin value = C_BUILTIN_CHAR;
    };
    template<> struct builtin_v_base<signed char> {
        static constexpr c_builtin value = C_BUILTIN_SCHAR;
    };
    template<> struct builtin_v_base<unsigned char> {
        static constexpr c_builtin value = C_BUILTIN_UCHAR;
    };
    template<> struct builtin_v_base<short> {
        static constexpr c_builtin value = C_BUILTIN_SHORT;
    };
    template<> struct builtin_v_base<unsigned short> {
        static constexpr c_builtin value = C_BUILTIN_USHORT;
    };
    template<> struct builtin_v_base<int> {
        static constexpr c_builtin value = C_BUILTIN_INT;
    };
    template<> struct builtin_v_base<unsigned int> {
        static constexpr c_builtin value = C_BUILTIN_UINT;
    };
    template<> struct builtin_v_base<long> {
        static constexpr c_builtin value = C_BUILTIN_LONG;
    };
    template<> struct builtin_v_base<unsigned long> {
        static constexpr c_builtin value = C_BUILTIN_ULONG;
    };
    template<> struct builtin_v_base<long long> {
        static constexpr c_builtin value = C_BUILTIN_LLONG;
    };
    template<> struct builtin_v_base<unsigned long long> {
        static constexpr c_builtin value = C_BUILTIN_ULLONG;
    };
    template<> struct builtin_v_base<float> {
        static constexpr c_builtin value = C_BUILTIN_FLOAT;
    };
    template<> struct builtin_v_base<double> {
        static constexpr c_builtin value = C_BUILTIN_DOUBLE;
    };
    template<> struct builtin_v_base<long double> {
        static constexpr c_builtin value = C_BUILTIN_LDOUBLE;
    };
    template<typename T> struct builtin_v_base<T *> {
        static constexpr c_builtin value = C_BUILTIN_PTR;
    };
    template<typename T> struct builtin_v_base<T &> {
        static constexpr c_builtin value = C_BUILTIN_REF;
    };

    /* need this hack because some toolchains are garbage */
    template<typename T> using eq_type = std::conditional_t<
        sizeof(T) == sizeof(char),
        signed char,
        std::conditional_t<
            sizeof(T) == sizeof(short),
            short,
            std::conditional_t<
                sizeof(T) == sizeof(int),
                int,
                std::conditional_t<
                    sizeof(T) == sizeof(long),
                    long,
                    long long
                >
            >
        >
    >;

    template<> struct builtin_v_base<wchar_t> {
        static constexpr c_builtin value =
            std::numeric_limits<wchar_t>::is_signed
                ? builtin_v_base<eq_type<wchar_t>>::value
                : builtin_v_base<std::make_unsigned_t<eq_type<wchar_t>>>::value;
    };
    template<> struct builtin_v_base<char16_t> {
        static constexpr c_builtin value =
            std::numeric_limits<char16_t>::is_signed
                ? builtin_v_base<eq_type<char16_t>>::value
                : builtin_v_base<std::make_unsigned_t<eq_type<char16_t>>>::value;
    };
    template<> struct builtin_v_base<char32_t> {
        static constexpr c_builtin value =
            std::numeric_limits<char32_t>::is_signed
                ? builtin_v_base<eq_type<char16_t>>::value
                : builtin_v_base<std::make_unsigned_t<eq_type<char32_t>>>::value;
    };
} /* namespace detail */

template<typename T>
static constexpr c_builtin builtin_v = detail::builtin_v_base<T>::value;

template<c_builtin t>
inline ffi_type *builtin_ffi_type() {
    return builtin_traits<t>::libffi_type();
}

enum c_cv {
    C_CV_CONST = 1 << 8,
    C_CV_VOLATILE = 1 << 9,
};

enum c_type_flags {
    C_TYPE_WEAK = 1 << 16,
    C_TYPE_CLOSURE = 1 << 17,
    C_TYPE_NOSIZE = 1 << 18,
    C_TYPE_VLA = 1 << 19
};

enum c_func_flags {
    C_FUNC_CDECL = 0,
    C_FUNC_FASTCALL,
    C_FUNC_STDCALL,
    C_FUNC_THISCALL,

    C_FUNC_VARIADIC = 1 << 8,
};

enum class c_object_type {
    INVALID = 0,
    FUNCTION,
    VARIABLE,
    CONSTANT,
    TYPEDEF,
    RECORD,
    ENUM,
    TYPE,
    PARAM,
};

enum class c_expr_type {
    INVALID = 0,
    INT,
    UINT,
    LONG,
    ULONG,
    LLONG,
    ULLONG,
    FLOAT,
    DOUBLE,
    LDOUBLE,
    STRING,
    CHAR,
    NULLPTR,
    BOOL,
    NAME,
    UNARY,
    BINARY,
    TERNARY
};

static inline int to_builtin_type(c_expr_type v) {
    switch (v) {
        case c_expr_type::INT:
            return C_BUILTIN_INT;
        case c_expr_type::UINT:
            return C_BUILTIN_UINT;
        case c_expr_type::LONG:
            return C_BUILTIN_LONG;
        case c_expr_type::ULONG:
            return C_BUILTIN_ULONG;
        case c_expr_type::LLONG:
            return C_BUILTIN_LLONG;
        case c_expr_type::ULLONG:
            return C_BUILTIN_ULLONG;
        case c_expr_type::FLOAT:
            return C_BUILTIN_FLOAT;
        case c_expr_type::DOUBLE:
            return C_BUILTIN_DOUBLE;
        case c_expr_type::LDOUBLE:
            return C_BUILTIN_LDOUBLE;
        case c_expr_type::CHAR:
            return C_BUILTIN_CHAR;
        case c_expr_type::BOOL:
            return C_BUILTIN_BOOL;
        default:
            break;
    }
    return C_BUILTIN_INVALID;
}

/* don't forget to update precedences in parser when adding to this */
enum class c_expr_binop {
    INVALID = 0,
    ADD,  // +
    SUB,  // -
    MUL,  // *
    DIV,  // /
    MOD,  // %

    EQ,   // ==
    NEQ,  // !=
    GT,   // >
    LT,   // <
    GE,   // >=
    LE,   // <=

    AND,  // &&
    OR,   // ||

    BAND, // &
    BOR,  // |
    BXOR, // ^
    LSH,  // <<
    RSH   // >>
};

enum class c_expr_unop {
    INVALID = 0,

    UNM,  // -
    UNP,  // +

    NOT,  // !
    BNOT  // ~
};

/* this stores primitive values in a way that can be passed to the evaluator */
union c_value {
    /* fp primitives, unknown size */
    long double ld;
    double d;
    float f;
    /* signed int primitives, unknown size */
    long long ll;
    long l;
    int i;
    short s;
    char c;
    signed char sc;
    /* unsigned int primitives, unknown size */
    unsigned long long ull;
    unsigned long ul;
    unsigned int u;
    unsigned short us;
    unsigned char uc;
    /* booleans */
    bool b;
};

struct c_expr {
    c_expr(int flags = 0): p_type(int(c_expr_type::INVALID) | flags) {}

    c_expr(c_expr const &) = delete;
    c_expr(c_expr &&e): p_type(e.p_type) {
        e.p_type = int(c_expr_type::INVALID);
        /* copy largest union */
        memcpy(&tern, &e.tern, sizeof(e.tern));
    }

    c_expr &operator=(c_expr const &) = delete;
    c_expr &operator=(c_expr &&e) {
        clear();
        p_type = e.p_type;
        e.p_type = int(c_expr_type::INVALID);
        memcpy(&tern, &e.tern, sizeof(e.tern));
        return *this;
    }

    ~c_expr() {
        clear();
    }

    struct unary {
        c_expr_unop op;
        c_expr *expr;
    };

    struct binary {
        c_expr_binop op;
        c_expr *lhs;
        c_expr *rhs;
    };

    struct ternary {
        c_expr *cond;
        c_expr *texpr;
        c_expr *fexpr;
    };

    union {
        unary un;
        binary bin;
        ternary tern;
        c_value val;
    };

    c_value eval(c_expr_type &et, bool promote = false) const;

    c_expr_type type() const {
        return c_expr_type(p_type & 0xFF);
    }

    void type(c_expr_type tp) {
        p_type ^= int(type());
        p_type |= int(tp);
    }

    bool owns() const {
        return !bool(p_type & C_TYPE_WEAK);
    }

private:
    int p_type;

    void clear() {
        if (!owns()) {
            return;
        }
        switch (type()) {
            case c_expr_type::UNARY:
                delete un.expr;
                break;
            case c_expr_type::BINARY:
                delete bin.lhs;
                delete bin.rhs;
                break;
            case c_expr_type::TERNARY:
                delete tern.cond;
                delete tern.texpr;
                delete tern.fexpr;
                break;
            default:
                break;
        }
    }
};

using c_object_cont_f = void (*)(std::string &, void *);

struct c_object {
    c_object() {}
    virtual ~c_object() {}

    virtual char const *name() const = 0;
    virtual c_object_type obj_type() const = 0;
    virtual void do_serialize(
        std::string &o, c_object_cont_f cont, void *data
    ) const = 0;

    std::string serialize() const {
        std::string out;
        do_serialize(out, nullptr, nullptr);
        return out;
    }

    template<typename T>
    T &as() {
        return *static_cast<T *>(this);
    }

    template<typename T>
    T const &as() const {
        return *static_cast<T const *>(this);
    }
};

struct c_function;
struct c_record;
struct c_enum;

struct c_type: c_object {
    c_type(int cbt, int qual):
        p_ptr{nullptr}, p_type{uint32_t(cbt) | uint32_t(qual)}
    {}

    c_type(c_type tp, int qual, int cbt = C_BUILTIN_PTR):
        p_ptr{new c_type{std::move(tp)}}, p_type{cbt | uint32_t(qual)}
    {}

    c_type(c_type tp, int qual, size_t arrlen, int flags):
        p_ptr{new c_type{std::move(tp)}}, p_asize{arrlen},
        p_type{C_BUILTIN_ARRAY | uint32_t(qual) | flags}
    {}

    c_type(c_type const *ctp, int qual, int cbt = C_BUILTIN_PTR):
        p_cptr{ctp}, p_type{cbt | C_TYPE_WEAK | uint32_t(qual)}
    {}

    c_type(c_function tp, int qual, bool cb = false);

    c_type(c_function const *ctp, int qual, bool cb = false):
        p_cfptr{ctp}, p_type{
            C_BUILTIN_FUNC | C_TYPE_WEAK |
            (cb ? C_TYPE_CLOSURE : 0) | uint32_t(qual)
        }
    {}

    c_type(c_record const *ctp, int qual):
        p_crec{ctp}, p_type{C_BUILTIN_RECORD | C_TYPE_WEAK | uint32_t(qual)}
    {}

    c_type(c_enum const *ctp, int qual):
        p_cenum{ctp}, p_type{C_BUILTIN_ENUM | C_TYPE_WEAK | uint32_t(qual)}
    {}

    c_type(c_type const &);
    c_type(c_type &&);

    c_type &operator=(c_type const &) = delete;
    c_type &operator=(c_type &&) = delete;

    ~c_type();

    c_object_type obj_type() const {
        return c_object_type::TYPE;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const;

    char const *name() const {
        switch (type()) {
            case C_BUILTIN_VOID:    return "void";
            case C_BUILTIN_CHAR:    return "char";
            case C_BUILTIN_SCHAR:   return "signed char";
            case C_BUILTIN_UCHAR:   return "unsigned char";
            case C_BUILTIN_SHORT:   return "short";
            case C_BUILTIN_USHORT:  return "unsigned short";
            case C_BUILTIN_INT:     return "int";
            case C_BUILTIN_UINT:    return "unsigned int";
            case C_BUILTIN_LONG:    return "long";
            case C_BUILTIN_ULONG:   return "unsigned long";
            case C_BUILTIN_LLONG:   return "long long";
            case C_BUILTIN_ULLONG:  return "unsigned long long";
            case C_BUILTIN_FLOAT:   return "float";
            case C_BUILTIN_DOUBLE:  return "double";
            case C_BUILTIN_LDOUBLE: return "long double";
            case C_BUILTIN_BOOL:    return "bool";
            case C_BUILTIN_VA_LIST: return "va_list";
            default: break;
        }
        return nullptr;
    }

    int type() const {
        return int(p_type & 0xFF);
    }

    int cv() const {
        return int(p_type & (0xFF << 8));
    }

    bool owns() const {
        return !bool(p_type & C_TYPE_WEAK);
    }

    bool vla() const {
        return p_type & C_TYPE_VLA;
    }

    bool unbounded() const {
        return p_type & C_TYPE_NOSIZE;
    }

    bool closure() const {
        switch (type()) {
            case C_BUILTIN_FUNC:
                return p_type & C_TYPE_CLOSURE;
            case C_BUILTIN_PTR:
            case C_BUILTIN_REF:
                return ptr_base().p_type & C_TYPE_CLOSURE;
            default:
                break;
        }
        return false;
    }

    bool arith() const {
        return type() >= C_BUILTIN_ENUM;
    }

    bool callable() const {
        auto tp = type();
        if (tp == C_BUILTIN_FUNC) {
            return true;
        }
        if ((tp != C_BUILTIN_PTR) && (tp != C_BUILTIN_REF)) {
            return false;
        }
        return ptr_base().type() == C_BUILTIN_FUNC;
    }

    bool passable() const;

    bool integer() const {
        return arith() && (type() < C_BUILTIN_FLOAT);
    }

    bool ptr_like() const {
        auto tp = type();
        return ((tp == C_BUILTIN_PTR) || (tp == C_BUILTIN_ARRAY));
    }

    bool is_unsigned() const {
        auto *p = libffi_type();
        return (
            (p == &ffi_type_uint8) ||
            (p == &ffi_type_uint16) ||
            (p == &ffi_type_uint32) ||
            (p == &ffi_type_uint64)
        );
    }

    void cv(int qual) {
        p_type |= uint32_t(qual);
    }

    c_type const &ptr_base() const {
        return *p_ptr;
    }

    /* only use if you know it's callable() */
    c_function const &function() const {
        if (type() == C_BUILTIN_FUNC) {
            return *p_fptr;
        }
        return *ptr_base().p_fptr;
    }

    c_record const &record() const {
        return *p_crec;
    }

    c_type const &deref() const {
        if (type() == C_BUILTIN_REF) {
            return ptr_base();
        }
        return *this;
    }

    ffi_type *libffi_type() const;

    size_t alloc_size() const;

    size_t array_size() const {
        return p_asize;
    }

    bool is_same(c_type const &other, bool ignore_cv = false) const;

    /* only use this with ref and ptr types */
    c_type as_type(int cbt) const {
        auto ret = c_type{*this};
        ret.p_type ^= ret.type();
        ret.p_type |= cbt;
        return ret;
    }

private:
    /* maybe a pointer? */
    union {
        c_type *p_ptr;
        c_function *p_fptr;
        c_type const *p_cptr;
        c_function const *p_cfptr;
        c_record const *p_crec;
        c_enum const *p_cenum;
    };
    size_t p_asize = 0;
    /*
     * 8 bits: type type (builtin/regular)
     * 8 bits: qualifier
     * 8 bits: ownership
     */
    uint32_t p_type;
};

struct c_param: c_object {
    c_param(std::string pname, c_type type):
        p_name{std::move(pname)}, p_type{std::move(type)}
    {}

    c_object_type obj_type() const {
        return c_object_type::PARAM;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const;

    char const *name() const {
        return p_name.c_str();
    }

    c_type const &type() const {
        return p_type;
    }

    ffi_type *libffi_type() const {
        return p_type.libffi_type();
    }

    size_t alloc_size() const {
        return p_type.alloc_size();
    }

private:
    std::string p_name;
    c_type p_type;
};

struct c_function: c_object {
    c_function(c_type result, std::vector<c_param> params, int flags):
        p_result{std::move(result)}, p_params{std::move(params)},
        p_flags{flags}
    {}

    c_object_type obj_type() const {
        return c_object_type::FUNCTION;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const;

    char const *name() const {
        return nullptr;
    }

    c_type const &result() const {
        return p_result;
    }

    std::vector<c_param> const &params() const {
        return p_params;
    }

    ffi_type *libffi_type() const {
        return &ffi_type_pointer;
    }

    size_t alloc_size() const {
        return sizeof(void *);
    }

    bool is_same(c_function const &other) const;

    bool variadic() const {
        return !!(p_flags & C_FUNC_VARIADIC);
    }

    int callconv() const {
        return p_flags & 0xF;
    }

private:
    c_type p_result;
    std::vector<c_param> p_params;
    int p_flags;
    bool p_variadic;
};

struct c_variable: c_object {
    c_variable(std::string vname, std::string sym, c_type vtype):
        p_name{std::move(vname)}, p_sname{std::move(sym)},
        p_type{std::move(vtype)}
    {}

    c_object_type obj_type() const {
        return c_object_type::VARIABLE;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const {
        p_type.do_serialize(o, cont, data);
    }

    char const *name() const {
        return p_name.c_str();
    }

    char const *sym() const {
        if (!p_sname.empty()) {
            return p_sname.c_str();
        }
        return p_name.c_str();
    }

    c_type const &type() const {
        return p_type;
    }

    ffi_type *libffi_type() const {
        return p_type.libffi_type();
    }

    size_t alloc_size() const {
        return p_type.alloc_size();
    }

private:
    std::string p_name;
    std::string p_sname;
    c_type p_type;
};

struct c_constant: c_object {
    c_constant(std::string cname, c_type ctype, c_value const &cval):
        p_name{std::move(cname)}, p_type{std::move(ctype)}, p_value{cval}
    {}

    c_object_type obj_type() const {
        return c_object_type::CONSTANT;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const {
        p_type.do_serialize(o, cont, data);
    }

    char const *name() const {
        return p_name.c_str();
    }

    c_type const &type() const {
        return p_type;
    }

    c_value const &value() const {
        return p_value;
    }

    ffi_type *libffi_type() const {
        return p_type.libffi_type();
    }

    size_t alloc_size() const {
        return p_type.alloc_size();
    }

private:
    std::string p_name;
    c_type p_type;
    c_value p_value;
};

struct c_typedef: c_object {
    c_typedef(std::string aname, c_type btype):
        p_name{std::move(aname)}, p_type{std::move(btype)}
    {}

    c_object_type obj_type() const {
        return c_object_type::TYPEDEF;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const {
        /* typedefs are resolved to their base type */
        p_type.do_serialize(o, cont, data);
    }

    char const *name() const {
        return p_name.c_str();
    }

    c_type const &type() const {
        return p_type;
    }

    ffi_type *libffi_type() const {
        return p_type.libffi_type();
    }

    size_t alloc_size() const {
        return p_type.alloc_size();
    }

private:
    std::string p_name;
    c_type p_type;
};

/* represents a record type: can be a struct or a union */
struct c_record: c_object {
    struct field {
        field(std::string nm, c_type &&tp):
            name{std::move(nm)}, type(std::move(tp))
        {}

        std::string name;
        c_type type;
    };

    c_record(std::string ename, std::vector<field> fields, bool is_uni = false):
        p_name{std::move(ename)}, p_uni{is_uni}
    {
        set_fields(std::move(fields));
    }

    c_record(std::string ename, bool is_uni = false):
        p_name{std::move(ename)}, p_uni{is_uni}
    {}

    c_object_type obj_type() const {
        return c_object_type::RECORD;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const {
        o += this->p_name;
        if (cont) {
            cont(o, data);
        }
    }

    char const *name() const {
        return p_name.c_str();
    }

    /* invalid for opaque structs */
    ffi_type *libffi_type() const {
        return const_cast<ffi_type *>(&p_ffi_type);
    }

    size_t alloc_size() const {
        return libffi_type()->size;
    }

    bool is_same(c_record const &other) const;

    ptrdiff_t field_offset(char const *fname, c_type const *&fld) const;

    bool opaque() const {
        return !p_elements;
    }

    bool passable() const {
        if (opaque() || is_union()) {
            return false;
        }
        bool ret = true;
        iter_fields([&ret](auto *, auto &type, size_t) {
            if (!type.passable()) {
                ret = false;
                return true;
            }
            return false;
        });
        return ret;
    }

    bool is_union() const {
        return p_uni;
    }

    std::vector<field> const &fields() const {
        return p_fields;
    }

    /* it is the responsibility of the caller to ensure we're not redefining */
    void set_fields(std::vector<field> fields);

    void metatype(int mt, int mf) {
        p_metatype = mt;
        p_metaflags = mf;
    }

    int metatype(int &flags) const {
        flags = p_metaflags;
        return p_metatype;
    }

    template<typename F>
    void iter_fields(F &&cb) const {
        bool end = false;
        iter_fields([](
            char const *fname, c_type const &type, size_t off, void *data
        ) {
            F &acb = *static_cast<F *>(data);
            return acb(fname, type, off);
        }, &cb, 0, end);
    }

private:
    size_t iter_fields(bool (*cb)(
        char const *fname, c_type const &type, size_t off, void *data
    ), void *data, size_t base, bool &end) const;

    std::string p_name;
    std::vector<field> p_fields{};
    std::unique_ptr<ffi_type *[]> p_elements{};
    std::unique_ptr<ffi_type *[]> p_felems{};
    ffi_type p_ffi_type{};
    ffi_type p_ffi_flex{};
    int p_metatype = LUA_REFNIL;
    int p_metaflags = 0;
    bool p_uni;
};

struct c_enum: c_object {
    struct field {
        field(std::string nm, int val):
            name{std::move(nm)}, value(val)
        {}

        std::string name;
        int value; /* FIXME: make a c_expr */
    };

    c_enum(std::string ename, std::vector<field> fields):
        p_name{std::move(ename)}
    {
        set_fields(std::move(fields));
    }

    c_enum(std::string ename): p_name{std::move(ename)} {}

    c_object_type obj_type() const {
        return c_object_type::ENUM;
    }

    void do_serialize(std::string &o, c_object_cont_f cont, void *data) const {
        o += this->p_name;
        if (cont) {
            cont(o, data);
        }
    }

    char const *name() const {
        return p_name.c_str();
    }

    std::vector<field> const &fields() const {
        return p_fields;
    }

    ffi_type *libffi_type() const {
        /* TODO: support for large enums */
        return &ffi_type_sint;
    }

    size_t alloc_size() const {
        return sizeof(int);
    }

    bool opaque() const {
        return p_opaque;
    }

    /* it is the responsibility of the caller to ensure we're not redefining */
    void set_fields(std::vector<field> fields) {
        assert(p_fields.empty());
        assert(p_opaque);

        p_fields = std::move(fields);
        p_opaque = false;
    }

private:
    std::string p_name;
    std::vector<field> p_fields{};
    bool p_opaque = true;
};

struct redefine_error: public std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct decl_store {
    decl_store() {}
    decl_store(decl_store &ds): p_base(&ds) {}
    ~decl_store() {
        drop();
    }

    decl_store &operator=(decl_store const &) = delete;

    /* takes ownership of the pointer */
    void add(c_object *decl);
    void commit();
    void drop();

    c_object const *lookup(char const *name) const;
    c_object *lookup(char const *name);

    std::string request_name() const;

    static decl_store &get_main(lua_State *L) {
        lua_getfield(L, LUA_REGISTRYINDEX, lua::CFFI_DECL_STOR);
        auto *ds = lua::touserdata<decl_store>(L, -1);
        assert(ds);
        lua_pop(L, 1);
        return *ds;
    }
private:
    decl_store *p_base = nullptr;
    std::vector<std::unique_ptr<c_object>> p_dlist{};
    std::unordered_map<
        char const *, c_object *, util::str_hash, util::str_equal
    > p_dmap{};
};

c_type from_lua_type(lua_State *L, int index);

} /* namespace ast */

#endif /* AST_HH */
