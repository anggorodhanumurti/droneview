#include "app/il2cpp/il2cpp_api.h"

#include "app/platform/platform.h"

namespace app::il2cpp {

namespace {

template <typename T>
T bind_fn(const core::TargetSymbolTable& table, const char* name) {
    return reinterpret_cast<T>(table.get(name));
}

bool all_present(const Il2CppApi& api) {
    return api.domain_get && api.domain_get_assemblies && api.assembly_get_image &&
           api.image_get_name && api.class_from_name && api.class_get_field_from_name &&
           api.class_get_method_from_name && api.field_get_offset &&
           api.field_static_get_value && api.field_static_set_value && api.array_new &&
           api.string_chars && api.string_new && api.type_get_name &&
           api.method_get_param && api.class_get_methods && api.method_get_name &&
           api.object_new && api.field_get_flags;
}

}  // namespace

bool Il2CppApi::bind(const core::TargetSymbolTable& table) {
    reset();
    domain_get = bind_fn<DomainGetFn>(table, "il2cpp_domain_get");
    domain_get_assemblies = bind_fn<DomainGetAssembliesFn>(table, "il2cpp_domain_get_assemblies");
    assembly_get_image = bind_fn<AssemblyGetImageFn>(table, "il2cpp_assembly_get_image");
    image_get_name = bind_fn<ImageGetNameFn>(table, "il2cpp_image_get_name");
    class_from_name = bind_fn<ClassFromNameFn>(table, "il2cpp_class_from_name");
    class_get_field_from_name =
        bind_fn<ClassGetFieldFromNameFn>(table, "il2cpp_class_get_field_from_name");
    class_get_method_from_name =
        bind_fn<ClassGetMethodFromNameFn>(table, "il2cpp_class_get_method_from_name");
    field_get_offset = bind_fn<FieldGetOffsetFn>(table, "il2cpp_field_get_offset");
    field_static_get_value =
        bind_fn<FieldStaticGetValueFn>(table, "il2cpp_field_static_get_value");
    field_static_set_value =
        bind_fn<FieldStaticSetValueFn>(table, "il2cpp_field_static_set_value");
    array_new = bind_fn<ArrayNewFn>(table, "il2cpp_array_new");
    string_chars = bind_fn<StringCharsFn>(table, "il2cpp_string_chars");
    string_new = bind_fn<StringNewFn>(table, "il2cpp_string_new");
    type_get_name = bind_fn<TypeGetNameFn>(table, "il2cpp_type_get_name");
    method_get_param = bind_fn<MethodGetParamFn>(table, "il2cpp_method_get_param");
    class_get_methods = bind_fn<ClassGetMethodsFn>(table, "il2cpp_class_get_methods");
    method_get_name = bind_fn<MethodGetNameFn>(table, "il2cpp_method_get_name");
    object_new = bind_fn<ObjectNewFn>(table, "il2cpp_object_new");
    field_get_flags = bind_fn<FieldGetFlagsFn>(table, "il2cpp_field_get_flags");
    return ready();
}

void Il2CppApi::bind_optional(const core::TargetSymbolTable& table) {
    thread_attach = bind_fn<ThreadAttachFn>(table, "il2cpp_thread_attach");
    thread_detach = bind_fn<ThreadDetachFn>(table, "il2cpp_thread_detach");
}

void Il2CppApi::reset() {
    domain_get = nullptr;
    domain_get_assemblies = nullptr;
    assembly_get_image = nullptr;
    image_get_name = nullptr;
    class_from_name = nullptr;
    class_get_field_from_name = nullptr;
    class_get_method_from_name = nullptr;
    field_get_offset = nullptr;
    field_static_get_value = nullptr;
    field_static_set_value = nullptr;
    array_new = nullptr;
    string_chars = nullptr;
    string_new = nullptr;
    type_get_name = nullptr;
    method_get_param = nullptr;
    class_get_methods = nullptr;
    method_get_name = nullptr;
    object_new = nullptr;
    field_get_flags = nullptr;
    thread_attach = nullptr;
    thread_detach = nullptr;
}

bool Il2CppApi::ready() const {
    return all_present(*this);
}

bool Il2CppApi::has_required_symbols() const {
    return domain_get && domain_get_assemblies && assembly_get_image && image_get_name &&
           class_from_name && class_get_field_from_name && class_get_method_from_name;
}

int Il2CppApi::bound_count() const {
    int count = 0;
    for (const auto* fn : {reinterpret_cast<const void*>(domain_get),
                           reinterpret_cast<const void*>(domain_get_assemblies),
                           reinterpret_cast<const void*>(assembly_get_image),
                           reinterpret_cast<const void*>(image_get_name),
                           reinterpret_cast<const void*>(class_from_name),
                           reinterpret_cast<const void*>(class_get_field_from_name),
                           reinterpret_cast<const void*>(class_get_method_from_name),
                           reinterpret_cast<const void*>(field_get_offset),
                           reinterpret_cast<const void*>(field_static_get_value),
                           reinterpret_cast<const void*>(field_static_set_value),
                           reinterpret_cast<const void*>(array_new),
                           reinterpret_cast<const void*>(string_chars),
                           reinterpret_cast<const void*>(string_new),
                           reinterpret_cast<const void*>(type_get_name),
                           reinterpret_cast<const void*>(method_get_param),
                           reinterpret_cast<const void*>(class_get_methods),
                           reinterpret_cast<const void*>(method_get_name),
                           reinterpret_cast<const void*>(object_new),
                           reinterpret_cast<const void*>(field_get_flags)}) {
        if (fn != nullptr) {
            ++count;
        }
    }
    return count;
}

Il2CppApi& api() {
    static Il2CppApi instance;
    return instance;
}

}  // namespace app::il2cpp

// Test bridge for the non-root Gadget workflow. The original libcstatic.so
// already resolved its 22 IL2CPP function pointers into .bss. A separately
// loaded reconstruction can borrow those pointers without re-resolving
// liblogic.so. This is intentionally not used by the normal library build.
extern "C" bool app_bind_live_il2cpp(const char* module_name) {
    constexpr app::uptr kGhidraBase = 0x100000;
    const char* name = (module_name != nullptr && *module_name != '\0')
                           ? module_name
                           : "libcstatic.so";
    const app::uptr base = app::platform::module_base(name);
    if (base == 0) {
        return false;
    }
    auto slot = [base](app::uptr ghidra_address) -> app::uptr {
        return *reinterpret_cast<app::uptr*>(base + (ghidra_address - kGhidraBase));
    };

    auto& api = app::il2cpp::api();
    api.reset();
    api.domain_get = reinterpret_cast<app::il2cpp::Il2CppApi::DomainGetFn>(slot(0x747d98));
    api.domain_get_assemblies =
        reinterpret_cast<app::il2cpp::Il2CppApi::DomainGetAssembliesFn>(slot(0x747d90));
    api.assembly_get_image =
        reinterpret_cast<app::il2cpp::Il2CppApi::AssemblyGetImageFn>(slot(0x747da0));
    api.image_get_name =
        reinterpret_cast<app::il2cpp::Il2CppApi::ImageGetNameFn>(slot(0x747da8));
    api.class_from_name =
        reinterpret_cast<app::il2cpp::Il2CppApi::ClassFromNameFn>(slot(0x747dd0));
    api.class_get_field_from_name =
        reinterpret_cast<app::il2cpp::Il2CppApi::ClassGetFieldFromNameFn>(slot(0x747de8));
    api.class_get_method_from_name =
        reinterpret_cast<app::il2cpp::Il2CppApi::ClassGetMethodFromNameFn>(slot(0x747e00));
    api.field_get_offset =
        reinterpret_cast<app::il2cpp::Il2CppApi::FieldGetOffsetFn>(slot(0x747e28));
    api.field_static_get_value =
        reinterpret_cast<app::il2cpp::Il2CppApi::FieldStaticGetValueFn>(slot(0x747df0));
    api.field_static_set_value =
        reinterpret_cast<app::il2cpp::Il2CppApi::FieldStaticSetValueFn>(slot(0x747df8));
    api.array_new = reinterpret_cast<app::il2cpp::Il2CppApi::ArrayNewFn>(slot(0x747de0));
    api.string_chars =
        reinterpret_cast<app::il2cpp::Il2CppApi::StringCharsFn>(slot(0x747e30));
    api.string_new = reinterpret_cast<app::il2cpp::Il2CppApi::StringNewFn>(slot(0x747e38));
    api.type_get_name =
        reinterpret_cast<app::il2cpp::Il2CppApi::TypeGetNameFn>(slot(0x747e20));
    api.method_get_param =
        reinterpret_cast<app::il2cpp::Il2CppApi::MethodGetParamFn>(slot(0x747e18));
    api.class_get_methods =
        reinterpret_cast<app::il2cpp::Il2CppApi::ClassGetMethodsFn>(slot(0x747e08));
    api.method_get_name =
        reinterpret_cast<app::il2cpp::Il2CppApi::MethodGetNameFn>(slot(0x747e10));
    api.object_new = reinterpret_cast<app::il2cpp::Il2CppApi::ObjectNewFn>(slot(0x747dd8));
    api.field_get_flags =
        reinterpret_cast<app::il2cpp::Il2CppApi::FieldGetFlagsFn>(slot(0x747e48));
    return api.ready();
}
