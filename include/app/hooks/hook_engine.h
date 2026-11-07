#pragma once

#include <cstddef>
#include <string>

#include "app/platform/platform.h"
#include "app/types.h"

namespace app::hooks {

struct HookResult {
    bool ok = false;
    void* original = nullptr;
    std::string error;

    explicit operator bool() const { return ok; }
};

class HookEngine {
public:
    // Install an inline hook at `target` using Dobby when available, then
    // fall back to GlossHook on Android.
    HookResult install(void* target, void* replacement);

    // Patch raw instructions. On Android this prefers DobbyCodePatch; otherwise
    // it uses mprotect + memcpy.
    bool patch_code(void* target, const u8* data, std::size_t size);

    bool set_memory_permission(void* address, std::size_t size,
                               platform::MemoryProtection protection);

    static bool dobby_available();
    static bool glosshook_available();
};

}  // namespace app::hooks
