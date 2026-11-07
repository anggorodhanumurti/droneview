#pragma once

#include <cstddef>
#include <cstdint>

#include "app/core/target_symbols.h"

namespace app::il2cpp {

// Opaque IL2CPP handles. We only need pointers here; the real layouts belong
// to the target Unity/IL2CPP version.
struct Domain;
struct Assembly;
struct Image;
struct Class;
struct FieldInfo;
struct MethodInfo;
struct Type;
struct Object;
struct String;
struct Array;

struct Il2CppApi {
    using DomainGetFn = Domain* (*)();
    using DomainGetAssembliesFn = const Assembly** (*)(const Domain*, std::size_t*);
    using AssemblyGetImageFn = const Image* (*)(const Assembly*);
    using ImageGetNameFn = const char* (*)(const Image*);
    using ClassFromNameFn = Class* (*)(const Image*, const char*, const char*);
    using ClassGetFieldFromNameFn = FieldInfo* (*)(Class*, const char*);
    using ClassGetMethodFromNameFn = const MethodInfo* (*)(Class*, const char*, int);
    using FieldGetOffsetFn = std::size_t (*)(FieldInfo*);
    using FieldStaticGetValueFn = void (*)(FieldInfo*, void*);
    using FieldStaticSetValueFn = void (*)(FieldInfo*, void*);
    using ArrayNewFn = Array* (*)(Class*, std::size_t);
    using StringCharsFn = const std::uint16_t* (*)(String*);
    using StringNewFn = String* (*)(const char*);
    using TypeGetNameFn = char* (*)(const Type*);
    using MethodGetParamFn = const Type* (*)(const MethodInfo*, int);
    using ClassGetMethodsFn = const MethodInfo* (*)(Class*, void**);
    using MethodGetNameFn = const char* (*)(const MethodInfo*);
    using ObjectNewFn = Object* (*)(Class*);
    using FieldGetFlagsFn = int (*)(FieldInfo*);

    DomainGetFn domain_get = nullptr;
    DomainGetAssembliesFn domain_get_assemblies = nullptr;
    AssemblyGetImageFn assembly_get_image = nullptr;
    ImageGetNameFn image_get_name = nullptr;
    ClassFromNameFn class_from_name = nullptr;
    ClassGetFieldFromNameFn class_get_field_from_name = nullptr;
    ClassGetMethodFromNameFn class_get_method_from_name = nullptr;
    FieldGetOffsetFn field_get_offset = nullptr;
    FieldStaticGetValueFn field_static_get_value = nullptr;
    FieldStaticSetValueFn field_static_set_value = nullptr;
    ArrayNewFn array_new = nullptr;
    StringCharsFn string_chars = nullptr;
    StringNewFn string_new = nullptr;
    TypeGetNameFn type_get_name = nullptr;
    MethodGetParamFn method_get_param = nullptr;
    ClassGetMethodsFn class_get_methods = nullptr;
    MethodGetNameFn method_get_name = nullptr;
    ObjectNewFn object_new = nullptr;
    FieldGetFlagsFn field_get_flags = nullptr;

    bool bind(const core::TargetSymbolTable& table);
    void reset();
    bool ready() const;
    bool has_required_symbols() const;
    int bound_count() const;
};

Il2CppApi& api();

}  // namespace app::il2cpp
