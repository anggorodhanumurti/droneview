#include "app/il2cpp/il2cpp_helpers.h"

#include <cstring>

namespace app::il2cpp {

namespace {

std::string utf16_to_utf8(const std::uint16_t* text) {
    std::string out;
    if (text == nullptr) {
        return out;
    }
    for (std::size_t i = 0; text[i] != 0; ++i) {
        std::uint32_t cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

}  // namespace

std::vector<const Image*> Helpers::images() {
    std::vector<const Image*> out;
    if (api_.domain_get == nullptr || api_.domain_get_assemblies == nullptr ||
        api_.assembly_get_image == nullptr) {
        return out;
    }
    const Domain* domain = api_.domain_get();
    if (domain == nullptr) {
        return out;
    }
    std::size_t count = 0;
    const Assembly** assemblies = api_.domain_get_assemblies(domain, &count);
    if (assemblies == nullptr) {
        return out;
    }
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (assemblies[i] == nullptr) {
            continue;
        }
        const Image* image = api_.assembly_get_image(assemblies[i]);
        if (image != nullptr) {
            out.push_back(image);
        }
    }
    return out;
}

const Image* Helpers::find_image(std::string_view name) {
    for (const Image* image : images()) {
        if (image_name(image) == name) {
            return image;
        }
    }
    return nullptr;
}

Class* Helpers::find_class(std::string_view image_name_value, std::string_view name_space,
                           std::string_view class_name_value) {
    if (api_.class_from_name == nullptr) {
        return nullptr;
    }
    const Image* image = image_name_value.empty() ? nullptr : find_image(image_name_value);
    if (image == nullptr && !image_name_value.empty()) {
        return nullptr;
    }
    const std::string ns(name_space);
    const std::string cls(class_name_value);
    return api_.class_from_name(image, ns.c_str(), cls.c_str());
}

Class* Helpers::find_class_any_image(std::string_view name_space, std::string_view class_name_value) {
    if (api_.class_from_name == nullptr) {
        return nullptr;
    }
    const std::string ns(name_space);
    const std::string cls(class_name_value);
    for (const Image* image : images()) {
        if (Class* klass = api_.class_from_name(image, ns.c_str(), cls.c_str())) {
            return klass;
        }
    }
    return nullptr;
}

FieldInfo* Helpers::find_field(Class* klass, std::string_view name) {
    if (api_.class_get_field_from_name == nullptr || klass == nullptr) {
        return nullptr;
    }
    const std::string owned(name);
    return api_.class_get_field_from_name(klass, owned.c_str());
}

const MethodInfo* Helpers::find_method(Class* klass, std::string_view name, int argc) {
    if (api_.class_get_method_from_name == nullptr || klass == nullptr) {
        return nullptr;
    }
    const std::string owned(name);
    return api_.class_get_method_from_name(klass, owned.c_str(), argc);
}

bool Helpers::get_static_field(FieldInfo* field, void* out, std::size_t size) {
    if (api_.field_static_get_value == nullptr || field == nullptr || out == nullptr || size == 0) {
        return false;
    }
    api_.field_static_get_value(field, out);
    return true;
}

bool Helpers::set_static_field(FieldInfo* field, const void* value, std::size_t size) {
    if (api_.field_static_set_value == nullptr || field == nullptr || value == nullptr || size == 0) {
        return false;
    }
    api_.field_static_set_value(field, const_cast<void*>(value));
    return true;
}

std::size_t Helpers::field_offset(FieldInfo* field) {
    return (api_.field_get_offset != nullptr && field != nullptr) ? api_.field_get_offset(field) : 0;
}

int Helpers::field_flags(FieldInfo* field) {
    return (api_.field_get_flags != nullptr && field != nullptr) ? api_.field_get_flags(field) : 0;
}

std::string Helpers::image_name(const Image* image) {
    if (api_.image_get_name == nullptr || image == nullptr) {
        return {};
    }
    const char* name = api_.image_get_name(image);
    return name != nullptr ? std::string(name) : std::string();
}

std::string Helpers::method_name(const MethodInfo* method) {
    if (api_.method_get_name == nullptr || method == nullptr) {
        return {};
    }
    const char* name = api_.method_get_name(method);
    return name != nullptr ? std::string(name) : std::string();
}

std::string Helpers::type_name(const Type* type) {
    if (api_.type_get_name == nullptr || type == nullptr) {
        return {};
    }
    const char* name = api_.type_get_name(type);
    std::string out = name != nullptr ? name : "";
    // IL2CPP returns an allocated string; free it if the API exposes the
    // matching free function. We intentionally do not call free here because
    // the reconstructed 22-symbol list has no il2cpp_free.
    return out;
}

std::string Helpers::string_to_utf8(String* string) {
    if (api_.string_chars == nullptr || string == nullptr) {
        return {};
    }
    return utf16_to_utf8(api_.string_chars(string));
}

String* Helpers::string_from_utf8(std::string_view text) {
    if (api_.string_new == nullptr) {
        return nullptr;
    }
    const std::string owned(text);
    return api_.string_new(owned.c_str());
}

std::vector<const MethodInfo*> Helpers::methods(Class* klass) {
    std::vector<const MethodInfo*> out;
    if (api_.class_get_methods == nullptr || klass == nullptr) {
        return out;
    }
    void* iterator = nullptr;
    while (const MethodInfo* method = api_.class_get_methods(klass, &iterator)) {
        out.push_back(method);
    }
    return out;
}

}  // namespace app::il2cpp
