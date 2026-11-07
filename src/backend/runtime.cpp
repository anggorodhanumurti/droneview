#include "runtime.h"

#include "app/platform/platform.h"
#include "droneview/backend.h"
#include "droneview/frontend.h"
#include "input_hook.h"

namespace dv::runtime {
namespace {

constexpr const char* kTag = "droneview";
bool g_initialized = false;

}  // namespace

bool ensure_ui() {
    if (g_initialized) {
        return true;
    }
    if (!frontend::initialize("droneview.ini", "droneview_.txt")) {
        app::platform::log_error(kTag, "frontend init failed");
        return false;
    }
    // Resolution runs on its own worker; this returns immediately.
    backend::initialize();
    g_initialized = true;
    app::platform::log_info(kTag, "UI initialized; Drone View menu active");
    return true;
}

void frame() {
    if (!g_initialized) {
        return;
    }
    // Retry the Unity input hook every ~30 frames until the JNI slot is valid.
    static int input_retry = 0;
    if (input_retry-- <= 0) {
        input_retry = input::install() ? 1 << 30 : 30;
    }
    frontend::frame();
}

bool ui_ready() {
    return g_initialized;
}

void shutdown() {
    if (!g_initialized) {
        return;
    }
    backend::shutdown();
    frontend::shutdown();
    g_initialized = false;
}

}  // namespace dv::runtime
