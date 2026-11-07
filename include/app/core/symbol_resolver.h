#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/elf/elf_module.h"
#include "app/types.h"

namespace app::core {

struct ResolvedSymbol {
    std::string name;
    uptr address = 0;
    std::size_t size = 0;

    explicit operator bool() const { return address != 0; }
};

class SymbolResolver {
public:
    explicit SymbolResolver(elf::ElfModule& module) : module_(module) {}

    std::optional<ResolvedSymbol> resolve(std::string_view name) const;
    std::vector<ResolvedSymbol> resolve_all(const std::vector<std::string>& names) const;

private:
    elf::ElfModule& module_;
};

// Find a module already loaded in the current process by basename or full path.
uptr find_loaded_module_base(std::string_view name);

// Return the module path containing an address, or an empty string.
std::string find_module_path(const void* address);

}  // namespace app::core
