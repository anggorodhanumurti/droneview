#include "app/core/target_symbols.h"

namespace app::core {

const std::vector<TargetSymbolInfo>& target_symbols() {
    static const std::vector<TargetSymbolInfo> symbols = {
        {"il2cpp_assembly_get_image", "DAT_00747da0", "assembly -> image"},
        {"il2cpp_domain_get", "DAT_00747d98", "current IL2CPP domain"},
        {"il2cpp_domain_get_assemblies", "DAT_00747d90", "enumerate assemblies"},
        {"il2cpp_image_get_name", "DAT_00747da8", "image/assembly name"},
        {"il2cpp_class_from_name", "DAT_00747dd0", "find class by namespace/name"},
        {"il2cpp_class_get_field_from_name", "DAT_00747de8", "find field by name"},
        {"il2cpp_class_get_method_from_name", "DAT_00747e00", "find method by name"},
        {"il2cpp_field_get_offset", "DAT_00747e28", "field offset"},
        {"il2cpp_field_static_get_value", "DAT_00747df0", "read static field"},
        {"il2cpp_field_static_set_value", "DAT_00747df8", "write static field"},
        {"il2cpp_array_new", "DAT_00747de0", "allocate IL2CPP array"},
        {"il2cpp_string_chars", "DAT_00747e30", "UTF-16 string data"},
        {"il2cpp_string_new", "DAT_00747e38", "create IL2CPP string"},
        {"il2cpp_string_new", "DAT_00747e40", "create IL2CPP string (alias 2)"},
        {"il2cpp_string_new", "DAT_00747d80", "create IL2CPP string (alias 3)"},
        {"il2cpp_string_new", "DAT_00747d88", "create IL2CPP string (alias 4)"},
        {"il2cpp_type_get_name", "DAT_00747e20", "type name"},
        {"il2cpp_method_get_param", "DAT_00747e18", "method parameter type"},
        {"il2cpp_class_get_methods", "DAT_00747e08", "enumerate methods"},
        {"il2cpp_method_get_name", "DAT_00747e10", "method name"},
        {"il2cpp_object_new", "DAT_00747dd8", "allocate managed object"},
        {"il2cpp_field_get_flags", "DAT_00747e48", "field flags"},
    };
    return symbols;
}

std::vector<std::string> target_symbol_names() {
    std::vector<std::string> names;
    names.reserve(target_symbols().size());
    for (const auto& symbol : target_symbols()) {
        names.emplace_back(symbol.api_name);
    }
    return names;
}

void TargetSymbolTable::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    symbols_.clear();
}

void TargetSymbolTable::set(std::string api_name, uptr address) {
    std::lock_guard<std::mutex> lock(mutex_);
    symbols_[std::move(api_name)] = address;
}

uptr TargetSymbolTable::get(std::string_view api_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = symbols_.find(std::string(api_name));
    return it == symbols_.end() ? 0 : it->second;
}

std::map<std::string, uptr> TargetSymbolTable::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return symbols_;
}

std::size_t TargetSymbolTable::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return symbols_.size();
}

TargetSymbolTable& target_symbol_table() {
    static TargetSymbolTable table;
    return table;
}

}  // namespace app::core
