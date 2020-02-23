#ifndef PARSER_HH
#define PARSER_HH

#include <cstdint>

#include <string>
#include <vector>
#include <memory>

namespace parser {

enum c_builtin {
    C_BUILTIN_INVALID = 0,

    C_BUILTIN_NOT,

    C_BUILTIN_PTR,

    C_BUILTIN_CHAR,
    C_BUILTIN_SHORT,
    C_BUILTIN_INT,
    C_BUILTIN_LONG,
    C_BUILTIN_LLONG,

    C_BUILTIN_INT8,
    C_BUILTIN_INT16,
    C_BUILTIN_INT32,
    C_BUILTIN_INT64,
    C_BUILTIN_INT128,

    C_BUILTIN_SIZE,
    C_BUILTIN_INTPTR,
    C_BUILTIN_PTRDIFF,

    C_BUILTIN_TIME,

    C_BUILTIN_FLOAT,
    C_BUILTIN_DOUBLE,

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

struct c_object {
    c_object(std::string oname = std::string{}): name{std::move(oname)} {}

    std::string name;

    virtual c_object_type obj_type() const = 0;
};

struct c_type: c_object {
    c_type(std::string tname, int cbt, int qual):
        c_object{std::move(tname)}, p_ptr{nullptr},
        p_type{uint32_t(cbt) | (uint32_t(qual) << 8)}
    {}

    c_type(c_type tp, int qual):
        c_object{}, p_ptr{std::make_unique<c_type>(std::move(tp))},
        p_type{uint32_t(qual) << 8}
    {}

    c_object_type obj_type() const {
        return c_object_type::TYPE;
    }

    int type() const {
        return int(p_type & 0xFF);
    }

    int cv() const {
        return int((p_type >> 8) & 0xFF);
    }

    void cv(int qual) {
        p_type |= uint32_t(qual) << 8;
    }

private:
    /* maybe a pointer? */
    std::unique_ptr<c_type> p_ptr;
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

void parse(std::string const &input);

}; /* namespace parser */

#endif /* PARSER_HH */
