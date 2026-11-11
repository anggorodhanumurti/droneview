#include "runtime.h"

#include <atomic>

#include "app/platform/platform.h"
#include "droneview/backend.h"
#include "droneview/frontend.h"
#include "input_hook.h"
#include "overlay_hook.h"

namespace dv::runtime {
namespace {

constexpr const char* kTag = "droneview";
std::atomic<bool> g_initialized{false};

}  // namespace

bool ensure_ui() {
    const bool runtime_ready = g_initialized.load(std::memory_order_acquire);
    if (runtime_ready && frontend::initialized()) {
        return true;
    }
    if (!frontend::initialize("droneview.ini", "droneview_.txt")) {
        app::platform::log_error(kTag, "frontend init failed");
        return false;
    }
    if (!runtime_ready) {
        // Resolution runs on its own worker; this returns immediately.
        backend::initialize();
        g_initialized.store(true, std::memory_order_release);
        app::platform::log_info(kTag, "UI initialized; Drone View menu active");
    } else {
        app::platform::log_info(kTag, "UI reinitialized for new EGL context");
    }
    return true;
}

void frame() {
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    // Retry the Unity input hook every ~30 frames until the JNI slot is valid.
    // Do not install any game hook before the initial checks pass.
    static int input_retry = 0;
    if (backend::initial_checks_passed() && input_retry-- <= 0) {
        input_retry = input::install() ? 1 << 30 : 30;
    }
    frontend::frame();
}

bool ui_ready() {
    return g_initialized.load(std::memory_order_acquire);
}

void shutdown() {
    // Disable the overlay path even if the UI never finished initializing.
    dv::overlay::shutdown();
    if (!g_initialized.load(std::memory_order_acquire)) {
        return;
    }
    backend::shutdown();
    frontend::shutdown();
    g_initialized.store(false, std::memory_order_release);
}

}  // namespace dv::runtime
