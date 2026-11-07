#include "app/core/symbol_resolver.h"

#include <dlfcn.h>
#include <link.h>

#include <algorithm>
#include <cstring>

namespace app::core {

namespace {

std::string basename_of(std::string_view path) {
    const auto slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

struct FindContext {
    std::string wanted;
    uptr base = 0;
};

int find_callback(struct dl_phdr_info* info, std::size_t /*size*/, void* data) {
    auto* ctx = static_cast<FindContext*>(data);
    if (ctx == nullptr || info == nullptr || info->dlpi_name == nullptr) {
        return 0;
    }
    const std::string_view path(info->dlpi_name);
    if (path.empty()) {
        return 0;
    }
    const std::string base = basename_of(path);
    if (base == ctx->wanted || path == ctx->wanted || path.find(ctx->wanted) != std::string_view::npos) {
        ctx->base = static_cast<uptr>(info->dlpi_addr);
        return 1;
    }
    return 0;
}

}  // namespace

std::optional<ResolvedSymbol> SymbolResolver::resolve(std::string_view name) const {
    if (!module_.is_open() || name.empty()) {
        return std::nullopt;
    }
    std::size_t size = 0;
    const uptr address = module_.symbol(name, &size);
    if (address == 0) {
        return std::nullopt;
    }
    return ResolvedSymbol{std::string(name), address, size};
}

std::vector<ResolvedSymbol> SymbolResolver::resolve_all(const std::vector<std::string>& names) const {
    std::vector<ResolvedSymbol> out;
    out.reserve(names.size());
    for (const auto& name : names) {
        if (auto symbol = resolve(name)) {
            out.push_back(std::move(*symbol));
        }
    }
    return out;
}

uptr find_loaded_module_base(std::string_view name) {
    if (name.empty()) {
        return 0;
    }
    FindContext ctx{basename_of(name), 0};
    dl_iterate_phdr(find_callback, &ctx);
    return ctx.base;
}

std::string find_module_path(const void* address) {
    if (address == nullptr) {
        return {};
    }
    Dl_info info{};
    if (dladdr(address, &info) == 0 || info.dli_fname == nullptr) {
        return {};
    }
    return info.dli_fname;
}

}  // namespace app::core
