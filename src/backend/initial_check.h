#pragma once

#include <string>
#include <string_view>

namespace dv::initial_check {

enum class Status {
    VersionPending,
    VersionUnsupported,
    GameLibraryMissing,
    Passed,
};

struct Result {
    Status status = Status::VersionPending;
    std::string version;
};

// One non-blocking preparation attempt. Runs before any game hooking:
//   1. read the installed application versionName,
//   2. require MLBB v2.2.16,
//   3. require at least one native IL2CPP game library to be loaded.
Result run();

// True when libfirst.so, libcsharp.so or liblogic.so is already mapped.
bool any_game_library_loaded();

bool is_supported_version(std::string_view version);

// Bottom status text shown when the detected version is not supported.
std::string unsupported_version_status(const std::string& version);

}  // namespace dv::initial_check
