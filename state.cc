#include "state.hh"

namespace state {

/* lua is not thread safe, so the FFI doesn't need to be either */

/* the list of declarations; actually stored */
static std::vector<std::unique_ptr<parser::c_object>> decl_list;

/* mapping for quick lookups */
static std::unordered_map<
    std::string, parser::c_object const *
> decl_map;

void add_decl(parser::c_object *decl) {
    decl_list.emplace_back(decl);
    auto &d = *decl_list.back();
    decl_map.emplace(d.name, &d);
}

parser::c_object const *lookup_decl(std::string const &name) {
    auto it = decl_map.find(name);
    if (it == decl_map.end()) {
        return nullptr;
    }
    return it->second;
}

} /* namespace state */
