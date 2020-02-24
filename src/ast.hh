#ifndef AST_HH
#define AST_HH

#include <vector>
#include <unordered_map>
#include <memory>

namespace ast {

enum c_builtin {
    C_BUILTIN_INVALID = 0,

    C_BUILTIN_NOT,

    C_BUILTIN_PTR,
    C_BUILTIN_FPTR,

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
        new (&p_ptr.ptr) std::unique_ptr<c_type>{nullptr};
    }

    c_type(c_type tp, int qual):
        c_object{}, p_type{C_BUILTIN_PTR | uint32_t(qual)}
    {
        new (&p_ptr.ptr) std::unique_ptr<c_type>{
            std::make_unique<c_type>(std::move(tp))
        };
    }

    c_type(c_type const &) = delete;
    c_type(c_type &&);

    c_type &operator=(c_type const &) = delete;
    c_type &operator=(c_type &&);

    c_type(c_function tp, int qual);

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

private:
    /* maybe a pointer? */
    union type_ptr {
        type_ptr() {}
        /* don't delete, members will be handled by c_type */
        ~type_ptr() {}

        std::unique_ptr<c_type> ptr;
        std::unique_ptr<c_function> fptr;
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

    void do_serialize(std::string &o) const {
        p_type.do_serialize(o);
        if (!this->name.empty()) {
            if (o.back() != '*') {
                o += ' ';
            }
            o += this->name;
        }
    }

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
    {
        /* allocate enough memory to store args + return */
        p_pvals.reserve(p_params.size() + 1);
    }

    c_object_type obj_type() const {
        return c_object_type::FUNCTION;
    }

    void do_serialize(std::string &o) const {
        do_serialize_full(o, false, 0);
    }

    void do_serialize_full(std::string &o, bool fptr, int cv) const {
        p_result.do_serialize(o);
        if (o.back() != '*') {
            o += ' ';
        }
        if (fptr) {
            o += "(*";
        } else {
            o += "(";
        }
        if (cv & C_CV_CONST) {
            if (o.back() != '(') {
                o += ' ';
            }
            o += "const";
        }
        if (cv & C_CV_VOLATILE) {
            if (o.back() != '(') {
                o += ' ';
            }
            o += "volatile";
        }
        o += ")(";
        bool first = true;
        for (auto &p: p_params) {
            if (!first) {
                o += ", ";
                first = false;
            }
            p.do_serialize(o);
        }
        o += ')';
    }

    c_type const &result() const {
        return p_result;
    }

    std::vector<c_param> const &params() const {
        return p_params;
    }

    std::vector<c_value> &pvals() {
        return p_pvals;
    }

private:
    c_type p_result;
    std::vector<c_param> p_params;
    std::vector<c_value> p_pvals;
};

inline c_type::c_type(c_function tp, int qual):
    c_object{}, p_type{C_BUILTIN_FPTR | uint32_t(qual)}
{
    new (&p_ptr.fptr) std::unique_ptr<c_function>{
        std::make_unique<c_function>(std::move(tp))
    };
}

inline c_type::~c_type() {
    if (type() == C_BUILTIN_FPTR) {
        using T = std::unique_ptr<c_function>;
        p_ptr.fptr.~T();
    } else if (type() == C_BUILTIN_PTR) {
        using T = std::unique_ptr<c_type>;
        p_ptr.ptr.~T();
    }
}

inline c_type::c_type(c_type &&v): p_type{v.p_type} {
    int tp = type();
    if (tp == C_BUILTIN_FPTR) {
        new (&p_ptr.fptr) std::unique_ptr<c_function>{
            std::move(v.p_ptr.fptr)
        };
    } else if (tp == C_BUILTIN_PTR) {
        new (&p_ptr.ptr) std::unique_ptr<c_type>{
            std::move(v.p_ptr.ptr)
        };
    }
}

inline c_type &c_type::operator=(c_type &&v) {
    if (type() == C_BUILTIN_FPTR) {
        using T = std::unique_ptr<c_function>;
        p_ptr.fptr.~T();
    } else if (type() == C_BUILTIN_PTR) {
        using T = std::unique_ptr<c_type>;
        p_ptr.ptr.~T();
    }

    p_type = v.p_type;
    if (type() == C_BUILTIN_FPTR) {
        new (&p_ptr.fptr) std::unique_ptr<c_function>{
            std::move(v.p_ptr.fptr)
        };
    } else if (type() == C_BUILTIN_PTR) {
        new (&p_ptr.ptr) std::unique_ptr<c_type>{
            std::move(v.p_ptr.ptr)
        };
    }

    return *this;
}

inline void c_type::do_serialize(std::string &o) const {
    int tcv = cv();
    int ttp = type();
    if (ttp == C_BUILTIN_PTR) {
        p_ptr.ptr->do_serialize(o);
        if (o.back() != '*') {
            o += ' ';
        }
        o += '*';
    } else if (ttp == C_BUILTIN_FPTR) {
        p_ptr.fptr->do_serialize_full(o, true, tcv);
        return;
    } else {
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
    }
    if (tcv & C_CV_CONST) {
        o += " const";
    }
    if (tcv & C_CV_VOLATILE) {
        o += " volatile";
    }
}

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

c_object *lookup_decl(std::string const &name);

} /* namespace ast */

#endif /* AST_HH */
