#ifndef AST_HH
#define AST_HH

#include <vector>
#include <unordered_map>
#include <memory>

namespace ast {

enum c_builtin {
    C_BUILTIN_INVALID = 0,

    C_BUILTIN_NOT,

    C_BUILTIN_VOID,

    C_BUILTIN_PTR,
    C_BUILTIN_FPTR,
    C_BUILTIN_FUNC,

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

    C_BUILTIN_TIME,

    C_BUILTIN_FLOAT,
    C_BUILTIN_DOUBLE,
    C_BUILTIN_LDOUBLE,

    C_BUILTIN_BOOL,
};

enum c_cv {
    C_CV_CONST = 1 << 8,
    C_CV_VOLATILE = 1 << 9,
    C_CV_UNSIGNED = 1 << 10,
    C_CV_SIGNED = 1 << 11,
};

enum class c_object_type {
    INVALID = 0,
    FUNCTION,
    VARIABLE,
    TYPEDEF,
    STRUCT,
    ENUM,
    TYPE,
    PARAM,
};

/* this is a universal union to store C values converted from
 * Lua values before they're passed to the function itself
 *
 * non-primitive Lua values are always boxed, so we know the max size
 */
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
    /* other types */
    size_t sz;
    /* booleans */
    bool b;
    /* pointer types */
    char const *str;
    void *ptr;
};

struct c_object {
    c_object(std::string oname = std::string{}): name{std::move(oname)} {}
    virtual ~c_object() {}

    std::string name;

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

struct c_type: c_object {
    c_type(std::string tname, int cbt, int qual):
        c_object{std::move(tname)},
        p_type{uint32_t(cbt) | uint32_t(qual)}
    {
        p_ptr.ptr = nullptr;
    }

    c_type(c_type tp, int qual):
        c_object{}, p_type{C_BUILTIN_PTR | uint32_t(qual)}
    {
        p_ptr.ptr = new c_type{std::move(tp)};
    }

    c_type(c_type const &);
    c_type(c_type &&);

    c_type &operator=(c_type const &) = delete;
    c_type &operator=(c_type &&) = delete;

    c_type(c_function tp, int qual, int cbt = C_BUILTIN_FPTR);

    ~c_type();

    c_object_type obj_type() const {
        return c_object_type::TYPE;
    }

    void do_serialize(std::string &o) const;

    int type() const {
        return int(p_type & 0xFF);
    }

    int cv() const {
        return int(p_type & (0xFF << 8));
    }

    void cv(int qual) {
        p_type |= uint32_t(qual);
    }

    c_type const &ptr_base() const {
        return *p_ptr.ptr;
    }

    c_function const &function() const {
        return *p_ptr.fptr;
    }

private:
    /* maybe a pointer? */
    union type_ptr {
        c_type *ptr;
        c_function *fptr;
    };
    type_ptr p_ptr;
    /*
     * 8 bits: type type (builtin/regular)
     * 8 bits: qualifier
     */
    uint32_t p_type;
};

struct c_param: c_object {
    c_param(std::string pname, c_type type):
        c_object{std::move(pname)}, p_type{std::move(type)}
    {}

    c_object_type obj_type() const {
        return c_object_type::PARAM;
    }

    void do_serialize(std::string &o) const;

    c_type const &type() const {
        return p_type;
    }

private:
    c_type p_type;
};

struct c_function: c_object {
    c_function(std::string fname, c_type result, std::vector<c_param> params):
        c_object{std::move(fname)}, p_result{std::move(result)},
        p_params{std::move(params)}
    {}

    c_object_type obj_type() const {
        return c_object_type::FUNCTION;
    }

    void do_serialize(std::string &o) const {
        do_serialize_full(o, false, 0);
    }

    void do_serialize_full(std::string &o, bool fptr, int cv) const;

    c_type const &result() const {
        return p_result;
    }

    std::vector<c_param> const &params() const {
        return p_params;
    }

private:
    c_type p_result;
    std::vector<c_param> p_params;
};

struct c_variable: c_object {
    c_variable(std::string vname, c_type vtype):
        c_object{std::move(vname)}, p_type{std::move(vtype)}
    {}

    c_object_type obj_type() const {
        return c_object_type::VARIABLE;
    }

    void do_serialize(std::string &o) const {
        p_type.do_serialize(o);
    }

    c_type const &type() const {
        return p_type;
    }

private:
    c_type p_type;
};

struct c_typedecl: c_object {
};

struct c_typedef: c_typedecl {
    c_object_type obj_type() const {
        return c_object_type::TYPEDEF;
    }
};

struct c_struct: c_typedecl {
    c_object_type obj_type() const {
        return c_object_type::STRUCT;
    }
};

struct c_enum: c_typedecl {
    c_object_type obj_type() const {
        return c_object_type::ENUM;
    }
};

/* takes unique ownership of the pointer */
void add_decl(c_object *decl);

c_object const *lookup_decl(std::string const &name);

} /* namespace ast */

#endif /* AST_HH */
