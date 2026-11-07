#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "app/il2cpp/il2cpp_api.h"

namespace app::il2cpp {

// Convenience helpers built on top of the 19 resolved IL2CPP functions.
// These are intentionally thin: they do not assume a specific game schema.
class Helpers {
public:
    explicit Helpers(Il2CppApi& api) : api_(api) {}

    bool ready() const { return api_.ready(); }
    Il2CppApi& api() { return api_; }

    std::vector<const Image*> images();
    const Image* find_image(std::string_view name);
    Class* find_class(std::string_view image_name, std::string_view name_space,
                      std::string_view class_name);
    Class* find_class_any_image(std::string_view name_space, std::string_view class_name);

    FieldInfo* find_field(Class* klass, std::string_view name);
    const MethodInfo* find_method(Class* klass, std::string_view name, int argc = -1);

    bool get_static_field(FieldInfo* field, void* out, std::size_t size);
    bool set_static_field(FieldInfo* field, const void* value, std::size_t size);
    std::size_t field_offset(FieldInfo* field);
    int field_flags(FieldInfo* field);

    std::string image_name(const Image* image);
    std::string method_name(const MethodInfo* method);
    std::string type_name(const Type* type);
    std::string string_to_utf8(String* string);
    String* string_from_utf8(std::string_view text);
    std::vector<const MethodInfo*> methods(Class* klass);

private:
    Il2CppApi& api_;
};

}  // namespace app::il2cpp
