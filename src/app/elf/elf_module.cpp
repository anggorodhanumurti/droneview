/*
 * Adapter for the ELF library layer.
 *
 * Original code:
 *   0x687b18  GlossOpen
 *   0x687e04  GlossSymbol
 *   0x687c4c  GlossGetLibBias
 *   0x688e48  CElf::Init
 *   0x68b4f8  xdl_open
 *
 * On Android the prebuilt GlossHook/xdl implementation is used. On the host
 * dlopen/dlsym/dlinfo provide the same conceptual API for tests.
 */
#include "app/elf/elf_module.h"

#include <dlfcn.h>
#include "app/platform/platform.h"
#include <link.h>

#include <utility>

#if APP_PLATFORM_ANDROID && APP_WITH_GLOSSHOOK
#include <Gloss.h>
#endif

namespace app::elf {

namespace {

#if !APP_PLATFORM_ANDROID
struct LinkMapResult {
    uptr bias = 0;
    std::string path;
};

LinkMapResult query_link_map(void* handle) {
    LinkMapResult result;
    struct link_map* map = nullptr;
    if (handle != nullptr && dlinfo(handle, RTLD_DI_LINKMAP, &map) == 0 && map != nullptr) {
        result.bias = static_cast<uptr>(map->l_addr);
        if (map->l_name != nullptr) {
            result.path = map->l_name;
        }
    }
    return result;
}
#endif

}  // namespace

ElfModule::~ElfModule() {
    close();
}

ElfModule::ElfModule(ElfModule&& other) noexcept
    : handle_(other.handle_), bias_(other.bias_), path_(std::move(other.path_)),
      via_glosshook_(other.via_glosshook_) {
    other.handle_ = nullptr;
    other.bias_ = 0;
    other.path_.clear();
    other.via_glosshook_ = false;
}

ElfModule& ElfModule::operator=(ElfModule&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        bias_ = other.bias_;
        path_ = std::move(other.path_);
        via_glosshook_ = other.via_glosshook_;
        other.handle_ = nullptr;
        other.bias_ = 0;
        other.path_.clear();
        other.via_glosshook_ = false;
    }
    return *this;
}

bool ElfModule::open(std::string_view name) {
    close();
    if (name.empty()) {
        return false;
    }
    const std::string owned(name);

#if APP_PLATFORM_ANDROID && APP_WITH_GLOSSHOOK
    GHandle handle = GlossOpen(owned.c_str());
    if (handle == nullptr) {
        // Fallback for host-side style tests / already-loaded modules.
        handle_ = dlopen(owned.c_str(), RTLD_NOW);
        if (handle_ == nullptr) {
            return false;
        }
        bias_ = platform::module_base(owned.c_str());
    } else {
        handle_ = handle;
        via_glosshook_ = true;
        bias_ = static_cast<uptr>(GlossGetLibBiasEx(handle));
        const char* path = GlossGetLibPath(handle);
        if (path != nullptr) {
            path_ = path;
        }
    }
#else
    handle_ = dlopen(owned.c_str(), RTLD_NOW);
    if (handle_ == nullptr) {
        return false;
    }
    const LinkMapResult info = query_link_map(handle_);
    bias_ = info.bias;
    path_ = info.path;
#endif

    return handle_ != nullptr;
}

void ElfModule::close() {
    if (handle_ == nullptr) {
        return;
    }
#if APP_PLATFORM_ANDROID && APP_WITH_GLOSSHOOK
    if (via_glosshook_) {
        GlossClose(static_cast<GHandle>(handle_), false);
    } else {
        dlclose(handle_);
    }
#else
    dlclose(handle_);
#endif
    handle_ = nullptr;
    bias_ = 0;
    path_.clear();
    via_glosshook_ = false;
}

uptr ElfModule::symbol(std::string_view name, std::size_t* size) const {
    if (handle_ == nullptr || name.empty()) {
        return 0;
    }
    const std::string owned(name);
#if APP_PLATFORM_ANDROID && APP_WITH_GLOSSHOOK
    if (via_glosshook_) {
        std::size_t symbol_size = 0;
        uptr address = static_cast<uptr>(GlossSymbol(static_cast<GHandle>(handle_), owned.c_str(), &symbol_size));
        if (address == 0) {
            address = reinterpret_cast<uptr>(dlsym(handle_, owned.c_str()));
        }
        if (size != nullptr) {
            *size = symbol_size;
        }
        return address;
    }
#endif
#if APP_PLATFORM_ANDROID
    if (size != nullptr) {
        *size = 0;
    }
    return reinterpret_cast<uptr>(dlsym(handle_, owned.c_str()));
#else
    uptr address = reinterpret_cast<uptr>(dlsym(handle_, owned.c_str()));
    if (size != nullptr) {
        *size = 0;
    }
    return address;
#endif
}

std::optional<ElfModule::MemoryRange> ElfModule::memory_range() const {
    if (handle_ == nullptr) {
        return std::nullopt;
    }
    struct Context {
        uptr bias = 0;
        std::string path;
        uptr start = 0;
        uptr end = 0;
        bool found = false;
    } context{bias_, path_, 0, 0, false};

    auto callback = [](struct dl_phdr_info* info, std::size_t, void* data) -> int {
        auto* ctx = static_cast<Context*>(data);
        if (info == nullptr) {
            return 0;
        }
        bool match = false;
        if (!ctx->path.empty() && info->dlpi_name != nullptr) {
            const std::string_view candidate(info->dlpi_name);
            const auto slash = candidate.find_last_of('/');
            const std::string_view basename = slash == std::string_view::npos
                                                  ? candidate
                                                  : candidate.substr(slash + 1);
            const auto wanted_slash = ctx->path.find_last_of('/');
            const std::string_view wanted = wanted_slash == std::string::npos
                                                ? std::string_view(ctx->path)
                                                : std::string_view(ctx->path).substr(wanted_slash + 1);
            match = (basename == wanted);
        }
        if (!match && ctx->bias != 0) {
            match = (static_cast<uptr>(info->dlpi_addr) == ctx->bias);
        }
        if (!match || info->dlpi_phdr == nullptr) {
            return 0;
        }
        uptr low = ~static_cast<uptr>(0);
        uptr high = 0;
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const auto& phdr = info->dlpi_phdr[i];
            if (phdr.p_type != PT_LOAD) {
                continue;
            }
            const uptr start = static_cast<uptr>(info->dlpi_addr) + phdr.p_vaddr;
            const uptr end = start + phdr.p_memsz;
            if (start < low) low = start;
            if (end > high) high = end;
        }
        if (high > low) {
            ctx->start = low;
            ctx->end = high;
            ctx->found = true;
            return 1;
        }
        return 0;
    };

    dl_iterate_phdr(callback, &context);
    if (!context.found) {
        return std::nullopt;
    }
    return MemoryRange{context.start, static_cast<std::size_t>(context.end - context.start)};
}

}  // namespace app::elf
