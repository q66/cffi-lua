#include "ast.hh"

namespace ast {

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
    int tp = type();
    if ((tp == C_BUILTIN_FPTR) || (tp == C_BUILTIN_FUNC)) {
        p_fptr = new c_function{*v.p_fptr};
    } else if (tp == C_BUILTIN_PTR) {
        p_ptr = new c_type{*v.p_ptr};
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

/* lua is not thread safe, so the FFI doesn't need to be either */

/* the list of declarations; actually stored */
static std::vector<std::unique_ptr<c_object>> decl_list;

/* mapping for quick lookups */
static std::unordered_map<std::string, c_object const *> decl_map;

void add_decl(c_object *decl) {
    decl_list.emplace_back(decl);
    auto &d = *decl_list.back();
    decl_map.emplace(d.name, &d);
}

c_object const *lookup_decl(std::string const &name) {
    auto it = decl_map.find(name);
    if (it == decl_map.end()) {
        return nullptr;
    }
    return it->second;
}

} /* namespace ast */
