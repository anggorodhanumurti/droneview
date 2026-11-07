#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include "app/platform/platform.h"
#include "overlay_hook.h"
#include "runtime.h"

namespace {

std::atomic<bool> g_stop{false};
std::once_flag g_start_once;

void background_main() {
    app::platform::log_info("droneview", "background thread started");
    while (!g_stop.load()) {
        if (!dv::overlay::installed()) {
            dv::overlay::install();
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    app::platform::log_info("droneview", "background thread stopped");
}

}  // namespace

extern "C" __attribute__((visibility("default"))) void dv_start(void) {
    std::call_once(g_start_once, [] {
        g_stop.store(false);
        std::thread(background_main).detach();
    });
}

extern "C" __attribute__((visibility("default"))) void dv_stop(void) {
    g_stop.store(true);
    dv::runtime::shutdown();
}

#if APP_PLATFORM_ANDROID && DRONEVIEW_AUTOSTART
namespace {
struct AutoStart {
    AutoStart() { dv_start(); }
};
AutoStart g_auto_start;
}  // namespace
#endif
