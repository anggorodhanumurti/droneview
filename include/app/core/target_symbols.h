#pragma once

#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "app/types.h"

namespace app::core {

struct TargetSymbolInfo {
    const char* api_name;
    const char* resolver_global;  // original DAT_00747... destination
    const char* note;
};

// The 22 symbols resolved by 0x4d24e0. Names were recovered by decoding the
// XOR-0x2E string constructor blocks around each GlossSymbol call.
const std::vector<TargetSymbolInfo>& target_symbols();
std::vector<std::string> target_symbol_names();

// Optional symbols. Missing entries are tolerated and never block binding.
const std::vector<TargetSymbolInfo>& optional_target_symbols();
std::vector<std::string> optional_target_symbol_names();

class TargetSymbolTable {
public:
    void clear();
    void set(std::string api_name, uptr address);
    uptr get(std::string_view api_name) const;
    std::map<std::string, uptr> snapshot() const;
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, uptr> symbols_;
};

TargetSymbolTable& target_symbol_table();

}  // namespace app::core
