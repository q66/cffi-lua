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

namespace ast {

enum c_builtin {
    C_BUILTIN_INVALID = 0,

    C_BUILTIN_VOID,

    C_BUILTIN_REF,
    C_BUILTIN_PTR,
    C_BUILTIN_FPTR,

    C_BUILTIN_FUNC,
    C_BUILTIN_STRUCT,

    /* everything past this matches type.scalar() */

    C_BUILTIN_ENUM,

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

    C_BUILTIN_WCHAR,
    C_BUILTIN_CHAR16,
    C_BUILTIN_CHAR32,

    C_BUILTIN_INT8,
    C_BUILTIN_INT16,
    C_BUILTIN_INT32,
    C_BUILTIN_INT64,

    C_BUILTIN_UINT8,
    C_BUILTIN_UINT16,
    C_BUILTIN_UINT32,
    C_BUILTIN_UINT64,

    C_BUILTIN_SIZE,
    C_BUILTIN_SSIZE,
    C_BUILTIN_INTPTR,
    C_BUILTIN_UINTPTR,
    C_BUILTIN_PTRDIFF,

    C_BUILTIN_TIME,

    C_BUILTIN_FLOAT,
    C_BUILTIN_DOUBLE,
    C_BUILTIN_LDOUBLE,

    C_BUILTIN_BOOL,
};

namespace detail {
    template<typename T>
    struct builtin_traits_base {
        using type = T;
        static ffi_type *libffi_type() { return ffi::ffi_traits<T>::type(); }
    };
} /* namespace detail */

/* only defined for scalar types with direct mappings */
template<c_builtin> struct builtin_traits;

template<> struct builtin_traits<C_BUILTIN_VOID>:
    detail::builtin_traits_base<void> {};

template<> struct builtin_traits<C_BUILTIN_PTR>:
    detail::builtin_traits_base<void *> {};

template<> struct builtin_traits<C_BUILTIN_REF>:
    detail::builtin_traits_base<char &> {};

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

template<> struct builtin_traits<C_BUILTIN_WCHAR>:
    detail::builtin_traits_base<wchar_t> {};

template<> struct builtin_traits<C_BUILTIN_CHAR16>:
    detail::builtin_traits_base<char16_t> {};

template<> struct builtin_traits<C_BUILTIN_CHAR32>:
    detail::builtin_traits_base<char32_t> {};

template<> struct builtin_traits<C_BUILTIN_INT8>:
    detail::builtin_traits_base<int8_t> {};

template<> struct builtin_traits<C_BUILTIN_INT16>:
    detail::builtin_traits_base<int16_t> {};

template<> struct builtin_traits<C_BUILTIN_INT32>:
    detail::builtin_traits_base<int32_t> {};

template<> struct builtin_traits<C_BUILTIN_INT64>:
    detail::builtin_traits_base<int64_t> {};

template<> struct builtin_traits<C_BUILTIN_UINT8>:
    detail::builtin_traits_base<uint8_t> {};

template<> struct builtin_traits<C_BUILTIN_UINT16>:
    detail::builtin_traits_base<uint16_t> {};

template<> struct builtin_traits<C_BUILTIN_UINT32>:
    detail::builtin_traits_base<uint32_t> {};

template<> struct builtin_traits<C_BUILTIN_UINT64>:
    detail::builtin_traits_base<uint64_t> {};

template<> struct builtin_traits<C_BUILTIN_SIZE>:
    detail::builtin_traits_base<size_t> {};

template<> struct builtin_traits<C_BUILTIN_SSIZE>:
    detail::builtin_traits_base<typename std::make_signed<size_t>::type> {};

template<> struct builtin_traits<C_BUILTIN_INTPTR>:
    detail::builtin_traits_base<intptr_t> {};

template<> struct builtin_traits<C_BUILTIN_UINTPTR>:
    detail::builtin_traits_base<uintptr_t> {};

template<> struct builtin_traits<C_BUILTIN_PTRDIFF>:
    detail::builtin_traits_base<ptrdiff_t> {};

template<> struct builtin_traits<C_BUILTIN_TIME>:
    detail::builtin_traits_base<time_t> {};

template<> struct builtin_traits<C_BUILTIN_FLOAT>:
    detail::builtin_traits_base<float> {};

template<> struct builtin_traits<C_BUILTIN_DOUBLE>:
    detail::builtin_traits_base<double> {};

template<> struct builtin_traits<C_BUILTIN_LDOUBLE>:
    detail::builtin_traits_base<long double> {};

template<> struct builtin_traits<C_BUILTIN_BOOL>:
    detail::builtin_traits_base<bool> {};

template<c_builtin t> using builtin_t = typename builtin_traits<t>::type;

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
    C_TYPE_CLOSURE = 1 << 17
};

enum class c_object_type {
    INVALID = 0,
    FUNCTION,
    VARIABLE,
    CONSTANT,
    TYPEDEF,
    STRUCT,
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
    STRING,
    CHAR,
    NULLPTR,
    BOOL,
    NAME,
    UNARY,
    BINARY,
    TERNARY
};

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

/* this is a universal union to store C values converted from
 * Lua values before they're passed to the function itself
 *
 * non-primitive Lua values are always boxed, so we know the max size
 */
union alignas(std::max_align_t) c_value {
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
    /* signed int primitives, fixed size */
    int64_t i64;
    int32_t i32;
    int16_t i16;
    int8_t i8;
    /* unsigned int primitives, fixed size */
    uint64_t u64;
    uint32_t u32;
    uint16_t u16;
    uint8_t u8;
    /* extra character types */
    wchar_t w;
    char16_t c16;
    char32_t c32;
    /* other types */
    size_t sz;
    typename std::make_signed<size_t>::type ssz;
    intptr_t ip;
    uintptr_t uip;
    ptrdiff_t pd;
    time_t t;
    /* booleans */
    bool b;
    /* pointer types */
    char const *str;
    void *ptr;
    void (*fptr)();
};

struct c_expr {
    c_expr(): type(c_expr_type::INVALID) {}

    c_expr(c_expr const &) = delete;
    c_expr(c_expr &&e): type(e.type) {
        e.type = c_expr_type::INVALID;
        /* copy largest union */
        memcpy(&tern, &e.tern, sizeof(e.tern));
    }

    c_expr &operator=(c_expr const &) = delete;
    c_expr &operator=(c_expr &&e) {
        clear();
        type = e.type;
        e.type = c_expr_type::INVALID;
        memcpy(&tern, &e.tern, sizeof(e.tern));
        return *this;
    }

    ~c_expr() {
        clear();
    }

    c_expr_type type;

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

    c_value eval() const;

private:
    void clear() {
        switch (type) {
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

struct c_object {
    c_object() {}
    virtual ~c_object() {}

    virtual char const *name() const = 0;
    virtual c_object_type obj_type() const = 0;
    virtual void do_serialize(std::string &o) const = 0;

    std::string serialize() const {
        std::string out;
        do_serialize(out);
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
struct c_struct;
struct c_enum;

struct c_type: c_object {
    c_type(std::string tname, int cbt, int qual):
        p_name{std::move(tname)}, p_ptr{nullptr},
        p_type{uint32_t(cbt) | uint32_t(qual)}
    {}

    c_type(c_type tp, int qual, int cbt = C_BUILTIN_PTR):
        p_name{}, p_ptr{new c_type{std::move(tp)}},
        p_type{cbt | uint32_t(qual)}
    {}

    c_type(c_type const *ctp, int qual, int cbt = C_BUILTIN_PTR):
        p_name{}, p_cptr{ctp},
        p_type{cbt | C_TYPE_WEAK | uint32_t(qual)}
    {}

    c_type(
        c_function const *ctp, int qual, int cbt = C_BUILTIN_FPTR,
        bool cb = false
    ):
        p_name{}, p_cfptr{ctp},
        p_type{cbt | C_TYPE_WEAK | (cb ? C_TYPE_CLOSURE : 0) | uint32_t(qual)}
    {}

    c_type(c_struct const *ctp, int qual):
        p_name{}, p_crec{ctp},
        p_type{C_BUILTIN_STRUCT | C_TYPE_WEAK | uint32_t(qual)}
    {}

    c_type(c_enum const *ctp, int qual):
        p_name{}, p_cenum{ctp},
        p_type{C_BUILTIN_ENUM | C_TYPE_WEAK | uint32_t(qual)}
    {}

    c_type(c_type const &);
    c_type(c_type &&);

    c_type &operator=(c_type const &) = delete;
    c_type &operator=(c_type &&) = delete;

    ~c_type();

    c_object_type obj_type() const {
        return c_object_type::TYPE;
    }

    void do_serialize(std::string &o) const;

    char const *name() const {
        return p_name.c_str();
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

    bool closure() const {
        return p_type & C_TYPE_CLOSURE;
    }

    bool scalar() const {
        return type() >= C_BUILTIN_ENUM;
    }

    bool integer() const {
        if (!scalar() || (type() > C_BUILTIN_TIME)) {
            return false;
        }
        if (type() == C_BUILTIN_TIME) {
            return std::numeric_limits<time_t>::is_integer;
        }
        return true;
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

    c_function const &function() const {
        return *p_fptr;
    }

    c_struct const &record() const {
        return *p_crec;
    }

    ffi_type *libffi_type() const;

    size_t alloc_size() const;

    bool is_same(c_type const &other, bool ignore_cv = false) const;
    bool converts_to(c_type const &other) const;

    /* only use this with ref and ptr types */
    c_type as_type(int cbt) const {
        auto ret = c_type{*this};
        ret.p_type ^= ret.type();
        ret.p_type |= cbt;
        return ret;
    }

private:
    std::string p_name;

    /* maybe a pointer? */
    union {
        c_type *p_ptr;
        c_function *p_fptr;
        c_type const *p_cptr;
        c_function const *p_cfptr;
        c_struct const *p_crec;
        c_enum const *p_cenum;
    };
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

    void do_serialize(std::string &o) const;

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
    c_function(
        std::string fname, c_type result, std::vector<c_param> params,
        bool variadic
    ):
        p_name{std::move(fname)}, p_result{std::move(result)},
        p_params{std::move(params)}, p_variadic{variadic}
    {}

    c_object_type obj_type() const {
        return c_object_type::FUNCTION;
    }

    void do_serialize(std::string &o) const {
        do_serialize_full(o, false, 0);
    }

    void do_serialize_full(std::string &o, bool fptr, int cv) const;

    char const *name() const {
        return p_name.c_str();
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
        return p_variadic;
    }

private:
    std::string p_name;
    c_type p_result;
    std::vector<c_param> p_params;
    bool p_variadic;
};

struct c_variable: c_object {
    c_variable(std::string vname, c_type vtype):
        p_name{std::move(vname)}, p_type{std::move(vtype)}
    {}

    c_object_type obj_type() const {
        return c_object_type::VARIABLE;
    }

    void do_serialize(std::string &o) const {
        p_type.do_serialize(o);
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

struct c_constant: c_object {
    c_constant(std::string cname, c_type ctype, c_value const &cval):
        p_name{std::move(cname)}, p_type{std::move(ctype)}, p_value{cval}
    {}

    c_object_type obj_type() const {
        return c_object_type::CONSTANT;
    }

    void do_serialize(std::string &o) const {
        p_type.do_serialize(o);
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

    void do_serialize(std::string &o) const {
        /* typedefs are resolved to their base type */
        p_type.do_serialize(o);
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

struct c_struct: c_object {
    struct field {
        field(std::string nm, c_type &&tp):
            name{std::move(nm)}, type(std::move(tp))
        {}

        std::string name;
        c_type type;
    };

    c_struct(std::string ename, std::vector<field> fields):
        p_name{std::move(ename)}
    {
        set_fields(std::move(fields));
    }

    c_struct(std::string ename): p_name{std::move(ename)} {}

    c_object_type obj_type() const {
        return c_object_type::STRUCT;
    }

    void do_serialize(std::string &o) const {
        o += this->p_name;
    }

    char const *name() const {
        return p_name.c_str();
    }

    /* invalid for opaque structs */
    ffi_type *libffi_type() const {
        return const_cast<ffi_type *>(&p_ffi_type);
    }

    size_t alloc_size() const {
        size_t s = libffi_type()->size;
        if (!s) {
            /* zero sized structs must allocate at least a byte */
            return 1;
        }
        return s;
    }

    bool is_same(c_struct const &other) const;

    ptrdiff_t field_offset(std::string const &fname, c_type const *&fld) const;

    bool opaque() const {
        return !p_elements;
    }

    std::vector<field> const &fields() const {
        return p_fields;
    }

    /* it is the responsibility of the caller to ensure we're not redefining */
    void set_fields(std::vector<field> fields);

private:
    std::string p_name;
    std::vector<field> p_fields{};
    std::unique_ptr<ffi_type *[]> p_elements{};
    ffi_type p_ffi_type{};
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

    void do_serialize(std::string &o) const {
        o += this->p_name;
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
