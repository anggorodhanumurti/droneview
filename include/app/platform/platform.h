#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <string>

namespace app::platform {

enum class MemoryProtection : std::uint32_t {
    None = 0,
    Read = 1 << 0,
    Write = 1 << 1,
    Execute = 1 << 2,
};

inline MemoryProtection operator|(MemoryProtection a, MemoryProtection b) {
    return static_cast<MemoryProtection>(static_cast<std::uint32_t>(a) |
                                         static_cast<std::uint32_t>(b));
}

void log_info(const char* tag, const char* fmt, ...);
void log_error(const char* tag, const char* fmt, ...);

void* open_library(const char* name);
void* find_symbol(void* handle, const char* name);
void close_library(void* handle);

// Returns the load bias of a module already loaded in the process, or 0.
std::uintptr_t module_base(const char* name);

// Returns the path of the module containing `addr`, or an empty string.
std::string module_path(const void* addr);

bool protect_memory(void* addr, std::size_t size, MemoryProtection prot);

int api_level();
std::uint64_t thread_id();

}  // namespace app::platform
