#include "droneview/backend.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/core/symbol_resolver.h"
#include "app/core/target_symbols.h"
#include "app/elf/elf_module.h"
#include "app/il2cpp/il2cpp_api.h"
#include "app/il2cpp/il2cpp_helpers.h"
#include "app/platform/platform.h"

namespace dv::backend {
namespace {

constexpr const char* kTag = "droneview";
// MLBB exposes the IL2CPP runtime through liblogic.so and libcsharp.so.
const char* const kCandidates[] = {"liblogic.so", "libcsharp.so"};

std::mutex g_mutex;
std::atomic<bool> g_ready{false};
std::atomic<bool> g_stop{false};
std::thread g_worker;
std::once_flag g_start_once;

std::string g_status = "waiting for liblogic.so / libcsharp.so";

app::il2cpp::FieldInfo* g_camera_field = nullptr;
void* g_show_camera = nullptr;
void* g_get_fov = nullptr;
void* g_set_fov = nullptr;

bool g_active = false;
float g_original_fov = 60.0f;
bool g_logged_enabled = false;

void set_status(const std::string& text) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_status = text;
}

void* method_pointer(const app::il2cpp::MethodInfo* method) {
    return method != nullptr ? *reinterpret_cast<void* const*>(method) : nullptr;
}

int expected_unique_symbols() {
    static const int value = [] {
        std::vector<std::string> names = app::core::target_symbol_names();
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return static_cast<int>(names.size());
    }();
    return value;
}

bool resolve_once() {
    app::core::TargetSymbolTable merged;
    const std::vector<std::string> names = app::core::target_symbol_names();
    const int expected = expected_unique_symbols();
    bool saw_library = false;

    // Never dlopen a runtime library early: only inspect libs the game already
    // loaded.
    for (const char* candidate : kCandidates) {
        if (app::platform::module_base(candidate) == 0) {
            continue;
        }
        saw_library = true;
        app::elf::ElfModule module;
        if (!module.open(candidate)) {
            continue;
        }
        app::core::SymbolResolver resolver(module);
        for (const auto& symbol : resolver.resolve_all(names)) {
            if (merged.get(symbol.name) == 0) {
                merged.set(symbol.name, symbol.address);
            }
        }
        app::platform::log_info(kTag, "IL2CPP from %s: merged %zu/%d", candidate,
                                merged.size(), expected);
        if (static_cast<int>(merged.size()) >= expected) {
            break;
        }
    }

    if (!saw_library) {
        set_status("waiting for liblogic.so / libcsharp.so");
        return false;
    }
    if (static_cast<int>(merged.size()) < expected ||
        !app::il2cpp::api().bind(merged)) {
        app::il2cpp::api().reset();
        set_status("IL2CPP incomplete (" + std::to_string(merged.size()) + "/" +
                   std::to_string(expected) + ")");
        return false;
    }

    app::il2cpp::Helpers helpers(app::il2cpp::api());

    app::il2cpp::Class* game_method =
        helpers.find_class("Assembly-CSharp.dll", "", "GameMethod");
    if (game_method == nullptr) {
        game_method = helpers.find_class_any_image("", "GameMethod");
    }
    if (game_method == nullptr) {
        set_status("GameMethod not found");
        return false;
    }

    app::il2cpp::FieldInfo* camera_field = helpers.find_field(game_method, "m_Camra");
    void* show_camera =
        method_pointer(helpers.find_method(game_method, "ShowCamera", 0));

    app::il2cpp::Class* camera_class =
        helpers.find_class("UnityEngine.CoreModule.dll", "UnityEngine", "Camera");
    if (camera_class == nullptr) {
        camera_class = helpers.find_class_any_image("UnityEngine", "Camera");
    }
    if (camera_class == nullptr) {
        set_status("UnityEngine.Camera not found");
        return false;
    }
    void* set_fov =
        method_pointer(helpers.find_method(camera_class, "set_fieldOfView", 1));
    void* get_fov =
        method_pointer(helpers.find_method(camera_class, "get_fieldOfView", 0));

    if (camera_field == nullptr || set_fov == nullptr) {
        set_status("camera bindings missing (m_Camra / set_fieldOfView)");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_camera_field = camera_field;
        g_show_camera = show_camera;
        g_get_fov = get_fov;
        g_set_fov = set_fov;
        g_status = "ready";
        g_ready.store(true, std::memory_order_release);
    }
    app::platform::log_info(kTag, "ready: m_Camra=%p show=%p getFov=%p setFov=%p",
                            static_cast<void*>(camera_field), show_camera, get_fov,
                            set_fov);
    return true;
}

void worker_main() {
    while (!g_stop.load(std::memory_order_acquire)) {
        if (resolve_once()) {
            return;
        }
        for (int i = 0; i < 15 && !g_stop.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

}  // namespace

void initialize() {
    std::call_once(g_start_once, [] {
        g_stop.store(false, std::memory_order_release);
        try {
            g_worker = std::thread(worker_main);
        } catch (...) {
            app::platform::log_error(kTag, "failed to start resolution worker");
        }
    });
}

void shutdown() {
    g_stop.store(true, std::memory_order_release);
    if (g_worker.joinable()) {
        g_worker.join();
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ready.store(false, std::memory_order_release);
    g_camera_field = nullptr;
    g_show_camera = nullptr;
    g_get_fov = nullptr;
    g_set_fov = nullptr;
    g_active = false;
    g_logged_enabled = false;
    g_status = "shutdown";
}

bool ready() {
    return g_ready.load(std::memory_order_acquire);
}

std::string status() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_status;
}

void apply(bool enabled, float value) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_acquire) || g_camera_field == nullptr ||
        g_set_fov == nullptr) {
        g_logged_enabled = false;
        return;
    }

    app::il2cpp::Helpers helpers(app::il2cpp::api());
    void* camera = nullptr;
    if (!helpers.get_static_field(g_camera_field, &camera, sizeof(camera)) ||
        camera == nullptr) {
        if (enabled && !g_logged_enabled) {
            app::platform::log_error(kTag, "GameMethod.m_Camra is null");
            g_logged_enabled = true;
        }
        return;
    }

    using GetFovFn = float (*)(void*);
    using SetFovFn = void (*)(void*, float);
    SetFovFn set_fov = reinterpret_cast<SetFovFn>(g_set_fov);

    if (!enabled) {
        if (g_active) {
            if (g_get_fov != nullptr && g_original_fov > 0.0f) {
                set_fov(camera, g_original_fov);
            }
            app::platform::log_info(kTag, "drone disabled, FOV restored to %.2f",
                                    g_original_fov);
            g_active = false;
        }
        g_logged_enabled = false;
        return;
    }

    if (!g_active) {
        g_original_fov = g_get_fov != nullptr
                             ? reinterpret_cast<GetFovFn>(g_get_fov)(camera)
                             : 60.0f;
        g_active = true;
        if (g_show_camera != nullptr) {
            reinterpret_cast<void (*)()>(g_show_camera)();
        }
        app::platform::log_info(kTag, "drone enabled, original FOV %.2f",
                                g_original_fov);
    }

    const float fov = std::max(20.0f, std::min(120.0f, 60.0f + value));
    set_fov(camera, fov);
    if (!g_logged_enabled) {
        app::platform::log_info(kTag, "applying FOV %.2f (value %.2f)", fov, value);
        g_logged_enabled = true;
    }
}

}  // namespace dv::backend
