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

// IL2CPP expects calls from a registered thread. The resolution worker and the
// render thread are not attached by Unity, so attach them before the first
// metadata/managed call. Best effort: the runtime may not export the symbol.
thread_local void* t_il2cpp_thread = nullptr;
thread_local bool t_attach_warned = false;

bool attach_current_thread() {
    if (t_il2cpp_thread != nullptr) {
        return true;
    }
    auto& api = app::il2cpp::api();
    if (api.thread_attach == nullptr || api.domain_get == nullptr) {
        if (!t_attach_warned) {
            app::platform::log_error(
                kTag,
                "il2cpp_thread_attach unavailable; continuing unattached (tid=%llu)",
                static_cast<unsigned long long>(app::platform::thread_id()));
            t_attach_warned = true;
        }
        return false;
    }
    app::il2cpp::Domain* domain = api.domain_get();
    if (domain == nullptr) {
        return false;
    }
    void* thread = api.thread_attach(domain);
    if (thread == nullptr) {
        if (!t_attach_warned) {
            app::platform::log_error(
                kTag, "il2cpp_thread_attach returned null (tid=%llu)",
                static_cast<unsigned long long>(app::platform::thread_id()));
            t_attach_warned = true;
        }
        return false;
    }
    t_il2cpp_thread = thread;
    app::platform::log_info(kTag, "il2cpp thread attached (tid=%llu thread=%p)",
                            static_cast<unsigned long long>(app::platform::thread_id()),
                            thread);
    return true;
}

void detach_current_thread() {
    if (t_il2cpp_thread == nullptr) {
        return;
    }
    auto& api = app::il2cpp::api();
    if (api.thread_detach != nullptr) {
        api.thread_detach(t_il2cpp_thread);
    }
    t_il2cpp_thread = nullptr;
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

int expected_optional_symbols() {
    static const int value = [] {
        std::vector<std::string> names = app::core::optional_target_symbol_names();
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());
        return static_cast<int>(names.size());
    }();
    return value;
}

bool resolve_once(int attempt) {
    app::core::TargetSymbolTable merged;
    app::core::TargetSymbolTable optional;
    const std::vector<std::string> names = app::core::target_symbol_names();
    const std::vector<std::string> optional_names =
        app::core::optional_target_symbol_names();
    const int expected = expected_unique_symbols();
    const int optional_expected = expected_optional_symbols();
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
        for (const auto& symbol : resolver.resolve_all(optional_names)) {
            if (optional.get(symbol.name) == 0) {
                optional.set(symbol.name, symbol.address);
            }
        }
        app::platform::log_info(
            kTag, "IL2CPP from %s: merged %zu/%d (+%zu/%d optional) tid=%llu",
            candidate, merged.size(), expected, optional.size(), optional_expected,
            static_cast<unsigned long long>(app::platform::thread_id()));
        if (static_cast<int>(merged.size()) >= expected &&
            static_cast<int>(optional.size()) >= optional_expected) {
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
    app::il2cpp::api().bind_optional(optional);
    app::platform::log_info(kTag, "IL2CPP thread attach: %s (tid=%llu)",
                            app::il2cpp::api().thread_attach != nullptr
                                ? "available"
                                : "missing",
                            static_cast<unsigned long long>(
                                app::platform::thread_id()));
    attach_current_thread();

    app::il2cpp::Helpers helpers(app::il2cpp::api());

    // Wait for the game assembly to be present before touching class metadata.
    // Do not fall back to scanning every image here: that enumeration is the
    // dangerous path during Unity startup.
    const app::il2cpp::Image* assembly_image =
        helpers.find_image("Assembly-CSharp.dll");
    if (assembly_image == nullptr) {
        set_status("waiting for Assembly-CSharp.dll");
        app::platform::log_info(
            kTag, "attempt %d: Assembly-CSharp.dll not loaded yet (tid=%llu)",
            attempt, static_cast<unsigned long long>(app::platform::thread_id()));
        return false;
    }
    app::il2cpp::Class* game_method =
        app::il2cpp::api().class_from_name(assembly_image, "", "GameMethod");
    if (game_method == nullptr) {
        set_status("GameMethod not found");
        app::platform::log_info(
            kTag, "attempt %d: GameMethod not found (tid=%llu)", attempt,
            static_cast<unsigned long long>(app::platform::thread_id()));
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
    app::platform::log_info(
        kTag, "ready: m_Camra=%p show=%p getFov=%p setFov=%p tid=%llu",
        static_cast<void*>(camera_field), show_camera, get_fov, set_fov,
        static_cast<unsigned long long>(app::platform::thread_id()));
    return true;
}

void worker_main() {
    // Give Unity time to finish loading the IL2CPP domain and assemblies before
    // this foreign thread reads metadata for the first time.
    for (int i = 0; i < 15 && !g_stop.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    int attempt = 0;
    while (!g_stop.load(std::memory_order_acquire)) {
        ++attempt;
        app::platform::log_info(
            kTag, "resolve attempt %d (tid=%llu)", attempt,
            static_cast<unsigned long long>(app::platform::thread_id()));
        if (resolve_once(attempt)) {
            detach_current_thread();
            return;
        }
        // Do not keep this native thread attached to IL2CPP while it sleeps
        // between retries.
        detach_current_thread();
        for (int i = 0; i < 15 && !g_stop.load(std::memory_order_acquire); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
    detach_current_thread();
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

void apply(bool enabled, float fov) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_ready.load(std::memory_order_acquire) || g_camera_field == nullptr ||
        g_set_fov == nullptr) {
        g_logged_enabled = false;
        return;
    }

    // Do not touch IL2CPP while the feature is off and nothing needs restoring.
    if (!enabled && !g_active) {
        g_logged_enabled = false;
        return;
    }

    // Best effort; the required metadata calls stay valid even if the target
    // does not export il2cpp_thread_attach.
    attach_current_thread();

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
        if (g_get_fov != nullptr && g_original_fov > 0.0f) {
            set_fov(camera, g_original_fov);
        }
        app::platform::log_info(kTag, "drone disabled, FOV restored to %.2f",
                                g_original_fov);
        g_active = false;
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
        app::platform::log_info(
            kTag, "drone enabled, original FOV %.2f (tid=%llu)", g_original_fov,
            static_cast<unsigned long long>(app::platform::thread_id()));
    }

    const float applied_fov = std::max(20.0f, std::min(120.0f, fov));
    set_fov(camera, applied_fov);
    if (!g_logged_enabled) {
        app::platform::log_info(kTag, "applying FOV %.2f", applied_fov);
        g_logged_enabled = true;
    }
}

}  // namespace dv::backend
