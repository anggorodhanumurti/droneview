#include "app/hooks/hook_engine.h"

#include <cstring>

#if APP_PLATFORM_ANDROID
#include <sys/mman.h>
#if APP_WITH_GLOSSHOOK
#include <Gloss.h>
#endif
// Dobby is optional at link time. Weak symbols let the Android build succeed
// without Dobby; GlossHook is used as a fallback when linked.
extern "C" {
int DobbyHook(void* address, void* fake_func, void** out_origin_func) __attribute__((weak));
int DobbyCodePatch(void* address, uint8_t* buffer, uint32_t buffer_size) __attribute__((weak));
}
#endif

namespace app::hooks {

bool HookEngine::dobby_available() {
#if APP_PLATFORM_ANDROID
    return DobbyHook != nullptr;
#else
    return false;
#endif
}

bool HookEngine::glosshook_available() {
#if APP_PLATFORM_ANDROID && APP_WITH_GLOSSHOOK
    return true;
#else
    return false;
#endif
}

HookResult HookEngine::install(void* target, void* replacement) {
    if (target == nullptr || replacement == nullptr) {
        return {false, nullptr, "target/replacement is null"};
    }

#if APP_PLATFORM_ANDROID
    void* original = nullptr;
    if (DobbyHook != nullptr) {
        const int rc = DobbyHook(target, replacement, &original);
        if (rc != 0) {
            return {false, original, "DobbyHook failed: " + std::to_string(rc)};
        }
        return {true, original, {}};
    }
#if APP_WITH_GLOSSHOOK
    GHook hook = GlossHook(target, replacement, &original);
    if (hook != nullptr) {
        return {true, original, {}};
    }
    return {false, original, "GlossHook failed"};
#else
    return {false, nullptr, "DobbyHook/GlossHook is not linked"};
#endif
#else
    return {false, nullptr, "inline hooks are not enabled on host builds"};
#endif
}

bool HookEngine::patch_code(void* target, const u8* data, std::size_t size) {
    if (target == nullptr || data == nullptr || size == 0) {
        return false;
    }

#if APP_PLATFORM_ANDROID
    if (DobbyCodePatch != nullptr &&
        DobbyCodePatch(target, const_cast<uint8_t*>(data), static_cast<uint32_t>(size)) == 0) {
        return true;
    }
#endif

#if APP_PLATFORM_ANDROID || APP_ALLOW_HOST_HOOKS
    if (!set_memory_permission(target, size,
                               platform::MemoryProtection::Read |
                                   platform::MemoryProtection::Write |
                                   platform::MemoryProtection::Execute)) {
        return false;
    }
    std::memcpy(target, data, size);
    // Flush the instruction cache on ARM. On x86 this is a no-op.
#if defined(__aarch64__) || defined(__arm__)
    __builtin___clear_cache(static_cast<char*>(target), static_cast<char*>(target) + size);
#endif
    return true;
#else
    (void)data;
    return false;
#endif
}

bool HookEngine::set_memory_permission(void* address, std::size_t size,
                                       platform::MemoryProtection protection) {
    return platform::protect_memory(address, size, protection);
}

}  // namespace app::hooks
