#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "app/types.h"

namespace app::elf {

// Adapter for the target library. On Android this is backed by GlossHook/xdl
// (GlossOpen, GlossSymbol, GlossGetLibBiasEx, GlossGetLibPath); on the host it
// is backed by dlopen/dlsym/dlinfo.
class ElfModule {
public:
    ElfModule() = default;
    ~ElfModule();

    ElfModule(const ElfModule&) = delete;
    ElfModule& operator=(const ElfModule&) = delete;

    ElfModule(ElfModule&& other) noexcept;
    ElfModule& operator=(ElfModule&& other) noexcept;

    bool open(std::string_view name);
    void close();

    bool is_open() const { return handle_ != nullptr; }
    void* native_handle() const { return handle_; }

    struct MemoryRange {
        uptr start = 0;
        std::size_t size = 0;

        bool valid() const { return start != 0 && size != 0; }
    };

    uptr symbol(std::string_view name, std::size_t* size = nullptr) const;
    uptr bias() const { return bias_; }
    const std::string& path() const { return path_; }
    std::optional<MemoryRange> memory_range() const;

private:
    void* handle_ = nullptr;
    uptr bias_ = 0;
    std::string path_;
    bool via_glosshook_ = false;
};

}  // namespace app::elf
