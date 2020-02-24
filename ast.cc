#include "ast.hh"

namespace ast {

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

c_object *lookup_decl(std::string const &name) {
    auto it = decl_map.find(name);
    if (it == decl_map.end()) {
        return nullptr;
    }
    return const_cast<c_object *>(it->second);
}

} /* namespace ast */
